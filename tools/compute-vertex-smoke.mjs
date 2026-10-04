// Exercises the shared C++/Slang RHI WebGPU proof, capturing both sides of a queue-ordered translation.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { chromium } from 'playwright-core';
import { PNG } from 'pngjs';

const folder = 'artifacts/animation/browser';
await mkdir(folder, { recursive: true });
const files = new Map();
for (const [extension, type] of [['html', 'text/html'], ['js', 'text/javascript'], ['wasm', 'application/wasm']]) {
    files.set(`/compute-proof.${extension}`, { type, data: await readFile(`build/web/compute-proof.${extension}`) });
}
const server = createServer((request, response) => {
    if (request.url === '/favicon.ico') { response.writeHead(204).end(); return; }
    const file = files.get(new URL(request.url, 'http://localhost').pathname);
    if (!file) { response.writeHead(404).end(); return; }
    response.writeHead(200, { 'Content-Type': file.type, 'Cache-Control': 'no-store' }).end(file.data);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const report = { errors: [], warnings: [], messages: [], captures: [] };
let browser;
try {
    browser = await chromium.launch({ channel: 'chrome', headless: true });
    report.browser = browser.version();
    const page = await browser.newPage({ viewport: { width: 320, height: 320 }, deviceScaleFactor: 1 });
    page.on('pageerror', error => { report.errors.push(error.stack || String(error)); console.error(error); });
    page.on('console', message => {
        report.messages.push(message.text());
        if (message.type() === 'error') { report.errors.push(message.text()); console.error(message.text()); }
        if (message.type() === 'warning') report.warnings.push(message.text());
    });
    await page.addInitScript(() => {
        window.addEventListener('error', () => { window.proofFailed = true; });
        window.addEventListener('unhandledrejection', () => { window.proofFailed = true; });
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/compute-proof.html`);
    await page.waitForFunction(() => window.proofFailed || Module.proofFrames >= 3, undefined, { timeout: 120000 });
    assert.equal(await page.evaluate(() => Boolean(window.proofFailed)), false, 'Proof startup failed; see report');
    report.adapter = await page.evaluate(async () => {
        const adapter = await navigator.gpu.requestAdapter();
        return { vendor: adapter.info.vendor, architecture: adapter.info.architecture,
            device: adapter.info.device, description: adapter.info.description,
            limits: { maxBufferSize: adapter.limits.maxBufferSize,
            maxStorageBufferBindingSize: adapter.limits.maxStorageBufferBindingSize,
            maxComputeInvocationsPerWorkgroup: adapter.limits.maxComputeInvocationsPerWorkgroup } };
    });
    // Fresh submitted frames avoid reading a canvas from before the changed command-ordered upload.
    for (const [name, offset, expectedX, emptyX] of [
        ['left', 0, 64, 166], ['right', .8, 166, 64], ['left-again', 0, 64, 166]
    ]) {
        const before = await page.evaluate(offset => {
            Module.proofOffset = offset;
            return Module.proofFrames;
        }, offset);
        await page.waitForFunction(before => Module.proofFrames >= before + 3, before);
        const path = `${folder}/compute-${name}.png`;
        const png = PNG.sync.read(await page.locator('#canvas').screenshot({ path }));
        assert.equal(png.data[(128 * png.width + expectedX) * 4 + 1], 255, `${name}: computed triangle visible`);
        assert.equal(png.data[(128 * png.width + emptyX) * 4 + 1], 0, `${name}: old placement cleared`);
        report.captures.push(path);
    }
    const unexpected = report.warnings.filter(text => !text.includes('powerPreference'));
    assert.deepEqual(unexpected, [], 'Unexpected browser validation warnings');
    assert.deepEqual(report.errors, [], 'Browser errors');
    report.passed = true;
    console.log('RHI WebGPU compute-to-vertex proof passed: left, right, left again.');
} finally {
    await writeFile(`${folder}/compute-report.json`, JSON.stringify(report, null, 2));
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
}
