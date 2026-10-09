// Real browser HTTP/IndexedDB/shared-memory/WebGPU proof; the Python driver owns an isolated content server.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';
import {chromium} from 'playwright-core';
import {startWebServer} from './serve-web.mjs';

const directory = 'artifacts/terrain-service/stage-2/web';
await mkdir(directory, {recursive:true});
assert.ok(process.env.OFG_TERRAIN_TEST_URL, 'Run through tools/terrain-client-test.py --browser');
const server = await startWebServer(0, Number(new URL(process.env.OFG_TERRAIN_TEST_URL).port));
const browser = await chromium.launch({channel:'chrome',headless:true});
const page = await browser.newPage({viewport:{width:1200,height:800}, recordVideo:{dir:directory,size:{width:900,height:600}}});
const origin = `http://127.0.0.1:${server.address().port}`;
const url = `${origin}/?demo=terrain&terrainService=/v1&island=demo&ui=0`;
const report = {errors:[], messages:[], states:[], requests:[]};
let expectedFailure = false;
page.on('console', message => {
    report.messages.push(message.text());
    if (message.type()==='error' && !(expectedFailure && message.text().includes('503'))) report.errors.push(message.text());
    if (message.type()==='warning' && !message.text().startsWith('The powerPreference option is currently ignored')) report.errors.push(message.text());
});
page.on('pageerror', error => report.errors.push(error.stack || String(error)));
page.on('request', request => { if (request.url().endsWith('.bin')) report.requests.push(request.url()); });
await page.addInitScript(() => {
    window.terrainStorageCalls = 0;
    const open = indexedDB.open.bind(indexedDB);
    indexedDB.open = (...args) => { if (args[0] === 'ofg-terrain-v1') ++window.terrainStorageCalls; return open(...args); };
});
// Require complete requested coverage and explicit error-free convergence, not a fixed arbitrary sleep.
async function settled(label) {
    await page.waitForFunction(() => Module.failed || Module.terrainError ||
        (Module.terrainState?.roots > 0 && Module.terrainState?.surfaceDepth >= 9 && Module.terrainState?.jobs === 0 &&
        Module.terrainState?.idle && Module.terrainState?.unresolved === 0), null, {timeout:150000});
    const state = await page.evaluate(() => ({...Module.terrainState, error:Module.terrainError,
        cache:Module.terrainCache, storageCalls:terrainStorageCalls, failedApp:Module.failed,
        shared:HEAPU8.buffer instanceof SharedArrayBuffer, isolated:crossOriginIsolated, workers:Object.keys(PThread.pthreads).length}));
    report.states.push({label,...state});
    console.log(label, state);
    assert.equal(state.failedApp, false);
    assert.equal(state.error, '');
    assert.equal(state.failed, 0);
    assert.ok(state.cpu <= 512 * 1024 * 1024);
    assert.ok(state.gpu <= 256 * 1024 * 1024);
    assert.equal(state.shared, true);
    assert.equal(state.isolated, true);
    assert.equal(state.workers, 5);
    await page.locator('#canvas').screenshot({path:`${directory}/${label}.png`});
    return state;
}
try {
    await page.goto(url);
    const cold = await settled('cold');
    assert.ok(cold.cache[1] > 0);
    assert.ok(cold.storageCalls > 0);
    // Same origin and immutable revision survive page destruction; no tile HTTP is possible during replay.
    let forbidden = 0;
    await page.route('**/*.bin', route => { ++forbidden; return route.fulfill({status:503,body:'offline'}); });
    await page.reload();
    const warm = await settled('offline-cache');
    assert.ok(warm.cache[0] > 0);
    assert.equal(warm.cache[1], 0);
    assert.equal(forbidden, 0);
    expectedFailure = true;
    await page.goto(url + '&skipCache=1');
    await page.waitForFunction(() => Module.terrainError, null, {timeout:60000});
    assert.ok(forbidden > 0);
    assert.equal(await page.evaluate(() => terrainStorageCalls), 0);
    await page.unroute('**/*.bin');
    await page.goto(url + '&skipCache=1');
    const bypass = await settled('bypass');
    expectedFailure = false;
    assert.equal(bypass.cache[0], 0);
    assert.ok(bypass.cache[2] > 0);
    assert.equal(bypass.storageCalls, 0);
    const previous = await page.evaluate(() => { Module.terrainCommand = 4; return Module.terrainState.publications; });
    await page.waitForFunction(value => Module.terrainState.publications > value && Module.terrainState.jobs === 0 && Module.terrainState.idle, previous, {timeout:60000});
    await page.locator('#canvas').screenshot({path:`${directory}/aerial.png`});
    await page.setViewportSize({width:1440,height:900});
    const frame = await page.evaluate(() => Module.frameCount);
    await page.waitForFunction(value => Module.frameCount > value + 5, frame);
    await page.locator('#canvas').screenshot({path:`${directory}/resized.png`});
    assert.deepEqual(report.errors, []);
} finally {
    try { report.last = await page.evaluate(() => ({state:Module.terrainState,error:Module.terrainError})); } catch {}
    await writeFile(`${directory}/report.json`, JSON.stringify(report,null,2));
    await browser.close();
    await new Promise(resolve => server.close(resolve));
}
