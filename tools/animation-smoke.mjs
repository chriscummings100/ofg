// Exercises actual WebGPU skinning and the rendered Animation panel; diagnostics are read-only.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {chromium} from 'playwright-core';
import {PNG} from 'pngjs';
import {startWebServer} from './serve-web.mjs';

const folder = fileURLToPath(new URL('../artifacts/animation/browser/', import.meta.url));
await mkdir(folder, {recursive:true});
const server = await startWebServer(0), origin = `http://127.0.0.1:${server.address().port}`;
const report = {errors:[], messages:[], captures:[], checks:[]};
let browser, context, page;
// Waits on submitted frames, including a failure escape rather than a fixed animation delay.
async function frames(count=3) {
    const before = await page.evaluate(()=>Module.frameCount);
    await page.waitForFunction(({before,count})=>Module.failed || Module.frameCount>=before+count,
        {before,count}, {timeout:120000});
    assert.equal(await page.evaluate(()=>Module.failed),false);
}
// Captures real presentation and retains playback state alongside each pose.
async function capture(name) {
    const bytes = await page.locator('#canvas').screenshot({path:`${folder}/${name}.png`});
    report.captures.push({name, animation:await page.evaluate(()=>Module.animations)});
    return PNG.sync.read(bytes);
}
// Clicks in logical canvas coordinates, accounting for the shell header.
async function click(x,y) {
    const box=await page.locator('#canvas').boundingBox();
    await page.mouse.move(box.x+x,box.y+y); await frames(1);
    await page.mouse.down(); await frames(2); await page.mouse.up(); await frames();
}
// Edits a slider through ImGui's ordinary numeric text input.
async function number(x,y,value) {
    await page.keyboard.down('Control'); await frames(); await click(x,y);
    await page.keyboard.up('Control'); await frames();
    await page.keyboard.down('Control'); await frames(2); await key('a');
    await page.keyboard.up('Control'); await frames(2);
    // Drain ImGui's key-event trickling between characters rather than queueing a long numeric string.
    for(const character of Number(value).toFixed(3)) await key(character);
    await key('Enter'); await frames(5);
}
// Counts changed scene pixels, excluding moving time labels and UI chrome.
function differences(a,b,rect) {
    let count=0;
    for(let y=Math.ceil(rect[1]);y<Math.floor(rect[3]);++y)
        for(let x=Math.ceil(rect[0]);x<Math.floor(rect[2]);++x) {
            const at=(y*a.width+x)*4;
            if([0,1,2].some(c=>Math.abs(a.data[at+c]-b.data[at+c])>2)) ++count;
        }
    return count;
}
// Chooses a source index using the real combo's keyboard navigation.
async function clip(index,name) {
    await click(1240,78);
    await key('End');
    await capture(`combo-end-${index}`);
    for(let i=44;i>index;--i) await key('ArrowUp');
    await key('Enter');
    await capture(`clip-selection-${index}`);
    assert.equal(await page.evaluate(()=>Module.animations[0].name),name);
}
// Drains down/up events before the next key, without holding arrows long enough for OS/UI repeat.
async function key(name) {
    await page.keyboard.press(name,{delay:20}); await frames(4);
}
try {
    browser=await chromium.launch({channel:'chrome',headless:!process.argv.includes('--headed')});
    report.browser=browser.version();
    context=await browser.newContext({viewport:{width:1440,height:940},deviceScaleFactor:1,
        recordVideo:{dir:folder,size:{width:1440,height:940}}});
    await context.addInitScript(()=>{
        const request=navigator.gpu.requestAdapter.bind(navigator.gpu);
        navigator.gpu.requestAdapter=async(...args)=>{
            const adapter=await request(...args); if(!adapter) return adapter;
            const create=adapter.requestDevice.bind(adapter);
            adapter.requestDevice=async(...args)=>{
                const device=await create(...args);
                window.animationDevice={vendor:adapter.info.vendor,architecture:adapter.info.architecture,
                    maxStorageBufferBindingSize:device.limits.maxStorageBufferBindingSize,
                    maxStorageBuffersPerShaderStage:device.limits.maxStorageBuffersPerShaderStage,
                    maxComputeInvocationsPerWorkgroup:device.limits.maxComputeInvocationsPerWorkgroup,
                    maxComputeWorkgroupsPerDimension:device.limits.maxComputeWorkgroupsPerDimension};
                return device;
            }; return adapter;
        };
    });
    page=await context.newPage();
    page.on('console',m=>{
        report.messages.push({type:m.type(),text:m.text()});
        if(m.type()==='error'||(m.type()==='warning'&&!m.text().startsWith('The powerPreference option is currently ignored')))
            report.errors.push(m.text());
    });
    page.on('pageerror',e=>report.errors.push(e.stack??e.message));
    await page.goto(`${origin}/?demo=character`);
    await page.waitForFunction(()=>Module.failed || Module.characterReady && Module.textureFrames>3,null,{timeout:180000});
    assert.equal(await page.evaluate(()=>Module.failed),false);
    report.device=await page.evaluate(()=>window.animationDevice);
    await frames();
    await capture('character-workspace');
    // Initial visual checkpoint also supports focused shader/host investigation.
    if (!process.argv.includes('--initial-only')) {
        await click(1290,28); // Animation tab beside Render Settings.
        await capture('animation-panel');
        const rect=await page.evaluate(()=>Module.uiState.sceneRect);
        const first=await capture('idle-playing-a'); await frames(15);
        const next=await capture('idle-playing-b');
        assert.ok(differences(first,next,rect)>50,'Idle must visibly deform geometry');
        await click(1135,101); // Pause.
        assert.equal(await page.evaluate(()=>Module.animations[0].playing),false);
        const paused=await capture('idle-paused'); await frames(10);
        assert.equal(differences(paused,await capture('idle-held'),rect),0);
        report.checks.push('idle motion and stable paused geometry');
        for (const [index,name] of [[44,'Walk_Loop'],[37,'Sprint_Loop'],[24,'Punch_Cross']]) {
            await clip(index,name);
            const duration=await page.evaluate(()=>Module.animations[0].duration);
            await number(1220,170,duration*.2);
            assert.ok(Math.abs(await page.evaluate(()=>Module.animations[0].time)-duration*.2)<.002);
            assert.equal(await page.evaluate(()=>Module.animations[0].playing),false);
            const a=await capture(`${name}-scrubbed`);
            await number(1220,170,duration*.6);
            assert.ok(Math.abs(await page.evaluate(()=>Module.animations[0].time)-duration*.6)<.002);
            const b=await capture(`${name}-later`);
            assert.ok(differences(a,b,rect)>200,`${name} poses should differ`);
            await click(1135,101);
            assert.equal(await page.evaluate(()=>Module.animations[0].playing),true);
            await frames(15);
            assert.ok(differences(b,await capture(`${name}-resumed`),rect)>100);
            await click(1135,101);
        }
        await click(1126,125); // Non-looping one-shot.
        assert.equal(await page.evaluate(()=>Module.animations[0].looping),false);
        await number(1220,170,0);
        await click(1135,101);
        await page.waitForFunction(()=>!Module.animations[0].playing,null,{timeout:30000});
        const ended=await page.evaluate(()=>Module.animations[0]);
        assert.equal(ended.time,ended.duration); await capture('one-shot-ended');
        await click(1185,101); // Stop.
        assert.equal(await page.evaluate(()=>Module.animations[0].time),0);
        await number(1220,148,0); await click(1135,101); await frames(10);
        assert.equal(await page.evaluate(()=>Module.animations[0].time),0);
        await number(1220,148,1);
        report.checks.push('clip selection, scrub pause, resume, one-shot end, stop and zero speed');
        await click(1126,125);
        if(!await page.evaluate(()=>Module.animations[0].playing)) await click(1135,101);
        const prior=await page.evaluate(()=>Module.animations[0].time);
        await click(30,9); await click(60,28); // Hide the scene via Window menu.
        assert.deepEqual(await page.evaluate(()=>Module.uiState.sceneRect),[0,0,0,0]);
        await frames(10);
        assert.notEqual(await page.evaluate(()=>Module.animations[0].time),prior);
        await click(30,9); await click(60,28);
        report.checks.push('hidden viewport continues animation');
        await page.goto(`${origin}/?demo=character&instances=2`);
        await page.waitForFunction(()=>Module.failed || Module.animations?.length===2 && Module.textureFrames>3,
            null,{timeout:180000});
        assert.equal(await page.evaluate(()=>Module.failed),false);
        await click(1290,28);
        await click(1135,101);
        assert.equal(await page.evaluate(()=>Module.animations[0].playing),false);
        assert.equal(await page.evaluate(()=>Module.animations[1].playing),true);
        const pairRect=await page.evaluate(()=>Module.uiState.sceneRect);
        const middle=(pairRect[0]+pairRect[2])/2;
        const left=[pairRect[0],pairRect[1],middle,pairRect[3]];
        const right=[middle,pairRect[1],pairRect[2],pairRect[3]];
        const pairA=await capture('independent-pair-a'); await frames(15);
        const pairB=await capture('independent-pair-b');
        assert.equal(differences(pairA,pairB,left),0);
        assert.ok(differences(pairA,pairB,right)>50);
        await click(1240,55); await key('End'); await key('Enter');
        await click(1135,101);
        assert.equal(await page.evaluate(()=>Module.animations[1].playing),false);
        const heldFirst=await page.evaluate(()=>Module.animations[0].time);
        await number(1220,170,.7);
        assert.equal(await page.evaluate(()=>Module.animations[0].time),heldFirst);
        assert.ok(Math.abs(await page.evaluate(()=>Module.animations[1].time)-.7)<.001);
        await capture('independent-pair-scrubbed');
        report.checks.push('two instances: left paused while right moves, independent animator selection and scrub');
        await page.setViewportSize({width:1100,height:760}); await frames(6);
        await capture('character-resized');
        await page.reload();
        await page.waitForFunction(()=>Module.failed || Module.animations?.length===2 && Module.textureFrames>3,
            null,{timeout:180000});
        assert.equal(await page.evaluate(()=>Module.failed),false);
        await capture('character-reloaded');
        report.checks.push('resize and reload');

        await page.goto(`${origin}/?demo=character&ui=0`);
        await page.waitForFunction(()=>Module.failed || Module.characterReady && Module.textureFrames>3,
            null,{timeout:180000});
        assert.equal(await page.evaluate(()=>Module.failed),false);
        await page.locator('#canvas').click();
        await page.waitForFunction(()=>document.pointerLockElement===Module.canvas);
        await page.keyboard.down('w');
        await page.waitForFunction(()=>Module.cameraPosition[2]>-3.8);
        await page.keyboard.up('w');
        await key('Escape');
        await page.waitForFunction(()=>document.pointerLockElement===null);
        await key('r');
        await page.waitForFunction(()=>Module.cameraPosition[0]===0 && Module.cameraPosition[1]===1 && Module.cameraPosition[2]===-4);
        await capture('full-canvas-reset');
        report.checks.push('full-canvas fly camera capture, movement, release and character reset');

        let heldRoute, announce;
        const requested=new Promise(resolve=>{announce=resolve;});
        await page.route('**/quaternius-ual1-standard.glb',route=>{heldRoute=route;announce();});
        await page.goto(`${origin}/?demo=character`); await requested;
        await frames(6);
        assert.equal(await page.evaluate(()=>Boolean(Module.characterReady)),false);
        await page.evaluate(()=>{Module.cancelCharacter=true;});
        await page.waitForFunction(()=>Module.characterCancelled);
        await heldRoute.continue().catch(()=>{});
        await frames(6);
        assert.equal(await page.evaluate(()=>Boolean(Module.characterReady)),false);
        assert.equal(await page.evaluate(()=>Module.failed),false);
        report.checks.push('frames continue during held library load; cancellation prevents late publication');
    }
    assert.deepEqual(report.errors,[]);
    report.passed=true;
} catch(error) {report.failure=error.stack??String(error);throw error;}
finally {
    if(page) report.video=await page.video()?.path();
    const reportName=process.argv.includes('--initial-only')?'animation-startup-report.json':'animation-report.json';
    await writeFile(`${folder}/${reportName}`,JSON.stringify(report,null,2));
    await context?.close(); await browser?.close(); await new Promise(resolve=>server.close(resolve));
}
console.log(`Animation smoke passed; ${folder}`);
