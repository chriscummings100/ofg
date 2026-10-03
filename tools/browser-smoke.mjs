// Focused browser smoke: scene objects and checkerboard, resize/reload and diagnostics.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';
import { PNG } from 'pngjs';
import { startWebServer } from './serve-web.mjs';

const artifacts = fileURLToPath(new URL('../artifacts/scene-rendering/browser/', import.meta.url));
await mkdir(artifacts, { recursive: true });
const report = { messages: [], errors: [], captures: [] };
const server = await startWebServer(0);
let browser;

// Checks visible square boundaries and alpha, accepting UNORM or sRGB presentation consistently per image.
function verifyCheckerboard(buffer) {
    const png = PNG.sync.read(buffer);
    const dark = png.data[0];
    const light = png.data[64 * 4];
    assert.ok([[32, 224], [99, 241]].some(([d, l]) => Math.abs(dark - d) <= 1 && Math.abs(light - l) <= 1),
        `Unexpected gray levels ${dark}, ${light}`);
    for (let y = 0; y < png.height; ++y) {
        for (let x = 0; x < png.width; ++x) {
            const offset = (y * png.width + x) * 4;
            const expected = (Math.floor(x / 64) + Math.floor(y / 64)) % 2 ? light : dark;
            for (let channel = 0; channel < 3; ++channel) {
                assert.ok(Math.abs(png.data[offset + channel] - expected) <= 1,
                    `Checkerboard mismatch at ${x},${y}, channel ${channel}`);
            }
            assert.equal(png.data[offset + 3], 255);
        }
    }
    return { width: png.width, height: png.height, dark, light };
}

// Checks scene structure without requiring identical native/browser edge rasterization or color encoding.
function verifyScene(buffer) {
    const png = PNG.sync.read(buffer);
    let background = 0, white = 0, gray = 0, cyan = 0;
    const first = [...png.data.subarray(0, 3)];
    for (let y = 0; y < png.height; ++y) {
        for (let x = 0; x < png.width; ++x) {
            const offset = (y * png.width + x) * 4;
            const [r, g, b, a] = png.data.subarray(offset, offset + 4);
            assert.equal(a, 255);
            if (Math.abs(r-first[0]) <= 1 && Math.abs(g-first[1]) <= 1 && Math.abs(b-first[2]) <= 1) ++background;
            else {
                // All geometry belongs near the center; the distant instance must remain off camera.
                assert.ok(x > png.width * 0.15 && x < png.width * 0.85 && y > png.height * 0.15 && y < png.height * 0.85);
                if (r > 180 && Math.abs(r-g) <= 2 && Math.abs(g-b) <= 2) ++white;
                if (r > 35 && r < 150 && Math.abs(r-g) <= 2 && Math.abs(g-b) <= 2) ++gray;
                if (b > r * 1.5 && g > r * 1.3 && b > 50) ++cyan;
            }
        }
    }
    const area = png.width * png.height;
    assert.ok(background > area * 0.7 && background < area * 0.98, 'Expected bounded visible cube geometry');
    assert.ok(white > area * 0.01 && gray > area * 0.01 && cyan > area * 0.005, 'Expected checker cells and isolated cyan material');
    return { width: png.width, height: png.height, background, white, gray, cyan };
}

// Waits for real frame submissions, then captures the canvas at its current physical pixel dimensions.
async function capture(page, name, verify = verifyScene) {
    await page.waitForFunction(() => Module.failed || Module.frameCount >= 3, null, { timeout: 60000 });
    assert.equal(await page.evaluate(() => Module.failed), false, 'Application reported failure');
    const buffer = await page.locator('#canvas').screenshot({ path: `${artifacts}/${name}.png` });
    const image = verify(buffer);
    report.captures.push({ name, ...image });
}

try {
    browser = await chromium.launch({ channel: 'chrome', headless: !process.argv.includes('--headed') });
    report.browser = browser.version();
    const page = await browser.newPage({ viewport: { width: 960, height: 680 }, deviceScaleFactor: 1 });
    page.on('console', message => {
        report.messages.push({ type: message.type(), text: message.text() });
        if (message.type() === 'error') report.errors.push(message.text());
        if (message.type() === 'warning' && !message.text().startsWith('The powerPreference option is currently ignored')) {
            report.errors.push(message.text());
        }
    });
    page.on('pageerror', error => { report.errors.push(error.stack ?? error.message); console.error(error.stack ?? error.message); });
    page.on('requestfailed', request => report.errors.push(`${request.url()}: ${request.failure()?.errorText}`));
    await page.goto(`http://127.0.0.1:${server.address().port}`, { waitUntil: 'load' });
    await capture(page, 'scene');
    report.adapter = await page.evaluate(() => Module.adapter);
    report.webgpu = await page.evaluate(async () => {
        const adapter = await navigator.gpu.requestAdapter();
        return {
            // This independent browser query is diagnostic; RHI may not expose an adapter description.
            vendor: adapter?.info.vendor, architecture: adapter?.info.architecture,
            device: adapter?.info.device, description: adapter?.info.description,
            features: adapter ? [...adapter.features] : [],
            maxTextureDimension2D: adapter?.limits.maxTextureDimension2D
        };
    });

    const beforeSceneResize = await page.evaluate(() => Module.frameCount);
    await page.setViewportSize({ width: 773, height: 517 });
    await page.waitForFunction(before => Module.canvas.width === 773 && Module.frameCount >= before + 3, beforeSceneResize);
    await capture(page, 'scene-resized');
    await page.reload({ waitUntil: 'load' });
    await capture(page, 'scene-reloaded');
    await page.goto(`http://127.0.0.1:${server.address().port}/?demo=checkerboard`, { waitUntil: 'load' });
    await capture(page, 'checkerboard', verifyCheckerboard);
    const beforeCheckerResize = await page.evaluate(() => Module.frameCount);
    await page.setViewportSize({ width: 960, height: 680 });
    await page.waitForFunction(before => Module.canvas.width === 960 && Module.frameCount >= before + 3, beforeCheckerResize);
    await capture(page, 'checkerboard-resized', verifyCheckerboard);
    await page.reload({ waitUntil: 'load' });
    await capture(page, 'checkerboard-reloaded', verifyCheckerboard);
    assert.deepEqual(report.errors, [], 'Browser reported errors');

    // A missing API must show a useful error, not start C++ and leave an unexplained blank canvas.
    const unavailable = await browser.newPage();
    await unavailable.addInitScript(() => Object.defineProperty(navigator, 'gpu', { value: undefined }));
    await unavailable.goto(`http://127.0.0.1:${server.address().port}`, { waitUntil: 'load' });
    await unavailable.waitForFunction(() => Module.failed);
    assert.match(await unavailable.locator('#status').innerText(), /WebGPU is unavailable/);
    report.unavailableApiMessage = true;
    await unavailable.close();
    report.passed = true;
    console.log(`WebGPU scene and checkerboard smoke passed in Chrome ${report.browser}.`);
} catch (error) {
    report.passed = false;
    report.failure = String(error.stack ?? error);
    throw error;
} finally {
    await writeFile(`${artifacts}/report.json`, JSON.stringify(report, null, 2));
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
}
