// Browser terrain integration proof, including actual worker execution and global teleports.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {chromium} from 'playwright-core';
import {startWebServer} from './serve-web.mjs';
const directory = 'artifacts/terrain/browser';
await mkdir(directory,{recursive:true});
const report={errors:[],messages:[],states:[]};
const stressSeconds = Number(process.argv[process.argv.indexOf('--stress-seconds') + 1]) || 0;
const server=await startWebServer(0);
const browser=await chromium.launch({channel:'chrome',headless:true});
let progressTimer;
let page;
try {
    page=await browser.newPage({viewport:{width:1200,height:800},
        ...(stressSeconds ? {recordVideo:{dir:directory,size:{width:900,height:600}}} : {})});
    page.on('console',m=>{report.messages.push(m.text());if(m.type()==='error'||(m.type()==='warning'&&!m.text().startsWith('The powerPreference option is currently ignored')))report.errors.push(m.text());});
    page.on('pageerror',e=>report.errors.push(e.stack??e.message));
    progressTimer=setInterval(async()=>{
        try { console.log('Terrain progress:',await page.evaluate(()=>globalThis.Module?.terrainState)); }
        catch { /* Navigation can temporarily replace the execution context. */ }
    },15000);
    await page.goto(`http://127.0.0.1:${server.address().port}/?demo=terrain&ui=0`);
    await page.waitForFunction(()=>Module.failed||(Module.terrainState?.surfaceDepth>=6&&Module.terrainState?.selected>500&&Module.terrainState?.unresolved===0&&Module.terrainState?.idle),null,{timeout:240000});
    assert.equal(await page.evaluate(()=>Module.failed),false,report.errors.join('\n'));
    report.states.push(await page.evaluate(()=>Module.terrainState));
    await page.locator('#canvas').screenshot({path:`${directory}/origin.png`});
    const previous = await page.evaluate(()=>{Module.terrainCommand=1;return Module.terrainState.publications;});
    await page.waitForFunction(n=>Module.failed||(Module.terrainState?.publications>n+200&&Module.terrainState?.loadingRoots===0&&(Module.terrainState?.surfaceDepth>=6&&Module.terrainState?.selected>500&&Module.terrainState?.unresolved===0&&Module.terrainState?.idle)),previous,{timeout:240000});
    report.states.push(await page.evaluate(()=>Module.terrainState));
    await page.locator('#canvas').screenshot({path:`${directory}/distant.png`});
    for(const state of report.states){assert.equal(state.failed,0);assert.ok(state.cpu<=256*1024*1024);assert.ok(state.gpu<=256*1024*1024);}
    if (stressSeconds) {
        await page.evaluate(seconds=>{
            Module.terrainHistory=[];
            Module.terrainRouteStart=performance.now();
            let next=0;
            const step=()=>{
                const elapsed=(performance.now()-Module.terrainRouteStart)/1000;
                Module.terrainRouteSeconds=elapsed;
                if(elapsed>=next) {Module.terrainHistory.push({seconds:elapsed,...Module.terrainState});next+=1;}
                if(elapsed<seconds) requestAnimationFrame(step);
                else {Module.terrainRouteSeconds=-1;Module.terrainCommand=3;Module.terrainRouteDone=true;}
            };
            requestAnimationFrame(step);
        },stressSeconds);
        await page.waitForFunction(()=>Module.failed||Module.terrainRouteDone,null,{timeout:(stressSeconds+60)*1000});
        assert.equal(await page.evaluate(()=>Module.failed),false);
        report.traversal=await page.evaluate(()=>Module.terrainHistory);
        await page.waitForFunction(()=>Module.failed||(Module.terrainState.roots===0&&Module.terrainState.jobs===0&&Module.terrainState.cpu===0&&Module.terrainState.gpu===0),null,{timeout:120000});
        report.drained=await page.evaluate(()=>Module.terrainState);
        for(const state of report.traversal){assert.equal(state.failed,0);assert.ok(state.cpu<=256*1024*1024);assert.ok(state.gpu<=256*1024*1024);}
    }
    else {
        await page.goto(`http://127.0.0.1:${server.address().port}/?demo=terrain&ui=1`);
        await page.waitForFunction(()=>Module.failed||(Module.terrainState?.surfaceDepth>=6&&Module.terrainState?.selected>500&&Module.terrainState?.unresolved===0&&Module.terrainState?.idle),null,{timeout:120000});
        assert.equal(await page.evaluate(()=>Module.failed),false);
        await page.locator('#canvas').screenshot({path:`${directory}/ui.png`});
        // Coordinates are from the inspected 1200x761 canvas capture; exercise the actual ImGui checkboxes.
        const canvas = await page.locator('#canvas').boundingBox();
        await page.mouse.click(canvas.x+77,canvas.y+120);
        await page.mouse.click(canvas.x+77,canvas.y+143);
        const toggled=await page.evaluate(()=>Module.frameCount);
        await page.waitForFunction(n=>Module.failed||Module.frameCount>n+3,toggled,{timeout:60000});
        await page.locator('#canvas').screenshot({path:`${directory}/lod-bounds.png`});
        assert.equal(await page.evaluate(()=>Module.failed),false);
        await page.setViewportSize({width:1440,height:900});
        const frame=await page.evaluate(()=>Module.frameCount);
        await page.waitForFunction(n=>Module.failed||Module.frameCount>n+3,frame,{timeout:60000});
        await page.locator('#canvas').screenshot({path:`${directory}/resized.png`});
        await page.reload();
        await page.waitForFunction(()=>Module.failed||(Module.terrainState?.surfaceDepth>=6&&Module.terrainState?.selected>500&&Module.terrainState?.unresolved===0&&Module.terrainState?.idle),null,{timeout:120000});
        assert.equal(await page.evaluate(()=>Module.failed),false);
        await page.locator('#canvas').screenshot({path:`${directory}/reloaded.png`});
    }
    assert.deepEqual(report.errors,[]);
} catch(error) {
    try {report.lastState=await page.evaluate(()=>globalThis.Module?.terrainState);} catch {}
    throw error;
} finally {
    clearInterval(progressTimer);
    await writeFile(`${directory}/${stressSeconds ? 'traversal' : 'report'}.json`,JSON.stringify(report,null,2));
    await browser.close();
    await new Promise(resolve=>server.close(resolve));
}
