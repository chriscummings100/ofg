// Real generation controls, held candidate HTTP and atomic revision adoption through browser WebGPU.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';
import {chromium} from 'playwright-core';
import {startWebServer} from './serve-web.mjs';

const directory = 'artifacts/terrain-service/stage-3/web';
await mkdir(directory, {recursive:true});
assert.ok(process.env.OFG_TERRAIN_TEST_URL, 'Run terrain-client-test.py --browser --editor');
const server = await startWebServer(0, Number(new URL(process.env.OFG_TERRAIN_TEST_URL).port));
const browser = await chromium.launch({channel:'chrome',headless:true});
const page = await browser.newPage({viewport:{width:1440,height:960},recordVideo:{dir:directory,size:{width:960,height:640}}});
const url = `http://127.0.0.1:${server.address().port}/?demo=terrain&terrainService=/v1&island=demo&ui=0`;
const report = {errors:[],states:[],messages:[]};
let expectedFailure = false;
page.on('console', message => {
    report.messages.push(message.text());
    if (message.type()==='error' && !(expectedFailure && message.text().includes('503'))) report.errors.push(message.text());
    if (message.type()==='warning' && !message.text().startsWith('The powerPreference option is currently ignored')) report.errors.push(message.text());
});
page.on('pageerror', error => report.errors.push(String(error)));
// Wait for complete requested coverage, retaining diagnostic state on any failure.
async function settled() {
    await page.waitForFunction(() => Module.failed || Module.terrainError ||
        (Module.terrainState?.surfaceDepth >= 7 && Module.terrainState?.jobs === 0 && Module.terrainState?.idle &&
         Module.terrainState?.unresolved === 0 && !Module.terrainGeneration?.replacing), null, {timeout:150000});
    assert.equal(await page.evaluate(() => Module.terrainError), '');
}
// Record both identities alongside a rendered frame and charged residency.
async function capture(label) {
    const state = await page.evaluate(() => ({generation:Module.terrainGeneration,stream:Module.terrainState,frame:Module.frameCount}));
    report.states.push({label,...state});
    assert.ok(state.stream.cpu <= 512 * 1024 * 1024);
    assert.ok(state.stream.gpu <= 256 * 1024 * 1024);
    await page.locator('#canvas').screenshot({path:`${directory}/${label}.png`});
    return state;
}
let releaseGate;
try {
    await page.goto(url);
    await settled();
    const framing = await page.evaluate(() => { Module.terrainCommand = 4; return Module.frameCount; });
    // The aerial camera is about 4 km above the source; its complete demand reaches depth 7, not ground-level 11.
    await page.waitForFunction(frame => Module.frameCount > frame + 5, framing);
    await settled();
    const before = await capture('before');
    const oldRevision = before.generation.displayed;
    const gate = new Promise(resolve => { releaseGate = resolve; });
    let held = false;
    await page.route('**/*.bin', async route => {
        if (!held && !route.request().url().includes(oldRevision)) {
            held = true;
            await gate;
        }
        await route.continue();
    });
    await page.evaluate(() => { Module.terrainCommand = 5; });
    await page.waitForFunction(old => Module.terrainGeneration?.published !== old &&
        Module.terrainGeneration?.replacing && Module.terrainState?.jobs === 1, oldRevision, {timeout:60000});
    assert.ok(held);
    const pinned = await capture('held-old-coverage');
    assert.equal(pinned.generation.displayed, oldRevision);
    assert.ok(pinned.stream.displayedLeaves > 0);
    await page.waitForFunction(frame => Module.frameCount > frame + 20, pinned.frame);
    assert.equal(await page.evaluate(() => Module.terrainGeneration.displayed), oldRevision);
    releaseGate();
    await page.waitForFunction(old => Module.terrainGeneration?.displayed !== old && !Module.terrainGeneration?.replacing,
        oldRevision, {timeout:60000});
    await settled();
    const changed = await capture('regenerated');
    assert.equal(changed.generation.displayed, changed.generation.published);
    assert.notEqual(changed.generation.displayed, oldRevision);
    await page.unroute('**/*.bin');
    expectedFailure = true;
    await page.route('**/*.bin', route => route.request().url().includes(changed.generation.displayed)
        ? route.continue() : route.fulfill({status:503,body:'held replacement failure'}));
    await page.evaluate(() => { Module.terrainCommand = 5; });
    await page.waitForFunction(() => Module.terrainError.includes('503'), null, {timeout:60000});
    const failed = await capture('failed-candidate-old-coverage');
    assert.equal(failed.generation.displayed, changed.generation.displayed);
    assert.ok(failed.stream.displayedLeaves > 0);
    await page.waitForFunction(frame => Module.frameCount > frame + 20, failed.frame);
    assert.equal(await page.evaluate(() => Module.terrainGeneration.displayed), changed.generation.displayed);
    await page.unroute('**/*.bin');
    await page.goto(url.replace('&ui=0',''));
    await settled();
    expectedFailure = false;
    await capture('editor');
    assert.equal(await page.evaluate(() => Object.keys(PThread.pthreads).length), 5);
    assert.deepEqual(report.errors, []);
} finally {
    releaseGate?.();
    try { report.last = await page.evaluate(() => ({generation:Module.terrainGeneration,stream:Module.terrainState,error:Module.terrainError})); } catch {}
    await writeFile(`${directory}/report.json`, JSON.stringify(report,null,2));
    await browser.close();
    await new Promise(resolve => server.close(resolve));
}
