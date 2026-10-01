// Focused browser smoke: shared checkerboard, resize and reload, with screenshots and console diagnostics.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';
import { PNG } from 'pngjs';
import { startWebServer } from './serve-web.mjs';

const artifacts = fileURLToPath(new URL('../artifacts/browser-smoke/', import.meta.url));
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

// Waits for real frame submissions, then captures the canvas at its current physical pixel dimensions.
async function capture(page, name) {
    await page.waitForFunction(() => Module.failed || Module.frameCount >= 3, null, { timeout: 60000 });
    assert.equal(await page.evaluate(() => Module.failed), false, 'Application reported failure');
    const buffer = await page.locator('#canvas').screenshot({ path: `${artifacts}/${name}.png` });
    const image = verifyCheckerboard(buffer);
    report.captures.push({ name, ...image });
}

try {
    browser = await chromium.launch({ channel: 'chrome', headless: !process.argv.includes('--headed') });
    report.browser = browser.version();
    const page = await browser.newPage({ viewport: { width: 960, height: 680 }, deviceScaleFactor: 1 });
    page.on('console', message => {
        report.messages.push({ type: message.type(), text: message.text() });
        if (message.type() === 'error') report.errors.push(message.text());
    });
    page.on('pageerror', error => report.errors.push(error.message));
    page.on('requestfailed', request => report.errors.push(`${request.url()}: ${request.failure()?.errorText}`));
    await page.goto(`http://127.0.0.1:${server.address().port}`, { waitUntil: 'load' });
    await capture(page, 'checkerboard');
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

    await page.setViewportSize({ width: 773, height: 517 });
    await page.waitForFunction(() => Module.canvas.width === 773);
    await capture(page, 'checkerboard-resized');
    await page.reload({ waitUntil: 'load' });
    await capture(page, 'checkerboard-reloaded');
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
    console.log(`WebGPU checkerboard smoke passed in Chrome ${report.browser}.`);
} catch (error) {
    report.passed = false;
    report.failure = String(error.stack ?? error);
    throw error;
} finally {
    await writeFile(`${artifacts}/report.json`, JSON.stringify(report, null, 2));
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
}
