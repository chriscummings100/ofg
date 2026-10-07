// Exercise the production C++ pthread workers and shared heap without loading graphics or the native test suite.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';
import {chromium} from 'playwright-core';
import {startWebServer} from './serve-web.mjs';

const server = await startWebServer(0);
const browser = await chromium.launch({channel: 'chrome', headless: true});
const report = {errors: []};
try {
    const page = await browser.newPage();
    page.on('pageerror', error => report.errors.push(error.message));
    page.on('console', message => {
        if (['error', 'warning'].includes(message.type())) report.errors.push(message.text());
    });
    await page.route('**/worker-check', route => route.fulfill({
        contentType: 'text/html',
        headers: {'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp'},
        body: `<!doctype html><link rel="icon" href="data:,"><title>C++ worker proof</title>
            <script>
                var Module = {printErr: text => console.error(text)};
                var heartbeat = 0;
                function beat() { ++heartbeat; requestAnimationFrame(beat); }
                requestAnimationFrame(beat);
            </script><script src="terrain-workers-proof.js"></script>`
    }));
    await page.goto(`http://127.0.0.1:${server.address().port}/worker-check`);
    await page.waitForFunction(() => Module.workerProof?.done || Module.workerProof?.error, null, {timeout: 60000});
    report.proof = await page.evaluate(() => ({...Module.workerProof, heartbeat, isolated: crossOriginIsolated}));
    assert.equal(report.proof.error, undefined);
    assert.equal(report.proof.done, true);
    assert.equal(report.proof.shared, true);
    assert.equal(report.proof.isolated, true);
    assert.equal(report.proof.stage, 5);
    assert.ok(report.proof.heartbeat >= 3);
    assert.ok(report.proof.indices > 0 && report.proof.vertices > 0);
    // Detached shutdown must return both generations of workers to the runtime pool.
    await page.waitForFunction(() => Object.keys(PThread.pthreads).length === 0, null, {timeout: 30000});
    report.drainedWorkers = await page.evaluate(() => Object.keys(PThread.pthreads).length);
    assert.deepEqual(report.errors, []);

    // Production shell must diagnose missing headers before trying to instantiate shared WASM.
    await page.route('**/index.html', async route => {
        const response = await route.fetch();
        const headers = response.headers();
        delete headers['cross-origin-opener-policy'];
        delete headers['cross-origin-embedder-policy'];
        await route.fulfill({response, headers});
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
    report.missingIsolation = await page.locator('#status').innerText();
    assert.match(report.missingIsolation, /cross-origin isolation/);
    assert.equal(await page.evaluate(() => Module.failed), true);
    assert.deepEqual(report.errors, []);
} finally {
    await mkdir('artifacts/terrain/browser', {recursive: true});
    await writeFile('artifacts/terrain/browser/workers.json', JSON.stringify(report, null, 2));
    await browser.close();
    await new Promise(resolve => server.close(resolve));
}
