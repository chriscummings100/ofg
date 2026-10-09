// Real FastScape controls and three paused terrain publications rendered through WebGPU.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';
import {chromium} from 'playwright-core';
import {startWebServer} from './serve-web.mjs';

const directory = 'artifacts/terrain-service/stage-4/web';
await mkdir(directory, {recursive:true});
const server = await startWebServer(0, Number(process.env.OFG_EROSION_PORT || 8768));
const browser = await chromium.launch({channel:'chrome',headless:true});
const page = await browser.newPage({viewport:{width:1440,height:960}, recordVideo:{dir:directory}});
const report = {errors:[], states:[]};
page.on('pageerror', e => report.errors.push(String(e)));
page.on('console', m => {
    if (m.type()==='error' || (m.type()==='warning' && !m.text().startsWith('The powerPreference option')))
        report.errors.push(m.text());
});
// Wait for a complete displayed revision and a quiet streaming planner.
async function settled() {
    await page.waitForFunction(() => Module.terrainError || Module.terrainGeneration?.error ||
        (Module.terrainState?.surfaceDepth >= 7 && !Module.terrainState.jobs && Module.terrainState.idle &&
         !Module.terrainState.unresolved && !Module.terrainGeneration.replacing), null, {timeout:180000});
    assert.equal(await page.evaluate(() => Module.terrainError), '');
    assert.equal(await page.evaluate(() => Module.terrainGeneration.error), '');
}
try {
    await page.goto(`http://127.0.0.1:${server.address().port}/?demo=terrain&terrainService=/v1&island=demo&ui=0`);
    await settled();
    const frame = await page.evaluate(() => {Module.terrainCommand=4; return Module.frameCount;});
    await page.waitForFunction(f=>Module.frameCount>f+5,frame);
    await settled();
    await page.evaluate(() => {Module.terrainCommand=6;});
    for (let step=0;step<3;step++) {
        if (step) await page.evaluate(() => {Module.terrainCommand=7;});
        await page.waitForFunction(n=>Module.terrainGeneration?.error ||
            (Module.terrainGeneration?.state==='paused' && Module.terrainGeneration.step===n &&
             Module.terrainGeneration.displayed===Module.terrainGeneration.published), step,{timeout:90000});
        await settled();
        const state=await page.evaluate(()=>({generation:Module.terrainGeneration,stream:Module.terrainState}));
        assert.equal(state.generation.years, step*1000);
        assert.ok(state.stream.cpu <= 512*1024*1024 && state.stream.gpu <= 256*1024*1024);
        report.states.push(state);
        await page.locator('#canvas').screenshot({path:`${directory}/step-${step}.png`});
    }
    await page.evaluate(()=>{Module.terrainCommand=10;});
    await page.waitForFunction(()=>Module.terrainGeneration?.state==='cancelled',null,{timeout:15000});
    assert.deepEqual(report.errors,[]);
} finally {
    try {report.last=await page.evaluate(()=>({generation:Module.terrainGeneration,error:Module.terrainError,stream:Module.terrainState}));}catch{}
    await writeFile(`${directory}/report.json`,JSON.stringify(report,null,2));
    await browser.close();
    await new Promise(resolve=>server.close(resolve));
}
