// Exercises actual WASM model import, held dependency fetches, cancellation and rest-pose WebGPU presentation.
import assert from 'node:assert/strict';
import {mkdir, readFile, writeFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {chromium} from 'playwright-core';
import {PNG} from 'pngjs';
import {startWebServer} from './serve-web.mjs';
const artifacts=fileURLToPath(new URL('../artifacts/models/browser/',import.meta.url));
await mkdir(artifacts,{recursive:true});
const server=await startWebServer(0), origin=`http://127.0.0.1:${server.address().port}`;
const report={errors:[],messages:[],captures:[]};let browser;
// Captures submitted model frames and verifies substantial geometry on both sides of the canvas.
async function capture(page,name) {
    // Wait for fresh submissions too: resize invalidates presentation even when readiness totals are already high.
    const before=await page.evaluate(()=>Module.frameCount);
    await page.waitForFunction(n=>Module.failed || (Module.textureFrames>=3 && Module.frameCount>=n+3),before,{timeout:120000});
    assert.equal(await page.evaluate(()=>Module.failed),false);
    const bytes=await page.locator('#canvas').screenshot({path:`${artifacts}/${name}.png`});
    const png=PNG.sync.read(bytes),foreground=[0,0];
    for(let y=0;y<png.height;++y)for(let x=0;x<png.width;++x){
        const at=(y*png.width+x)*4;
        if([0,1,2].some(c=>Math.abs(png.data[at+c]-png.data[c])>5))++foreground[x<png.width/2?0:1];
    }
    assert.ok(foreground.every(count=>count>1000),'Both imported cube nodes should be visible');
    const status=await page.evaluate(()=>Module.modelStatus);
    assert.match(status,/4 nodes, 1 meshes, 1 materials, 1 skins, 3 clips/);
    report.captures.push({name,foreground,status});
}
try {
    browser=await chromium.launch({channel:'chrome',headless:!process.argv.includes('--headed')});
    report.browser=browser.version();
    const page=await browser.newPage({viewport:{width:960,height:740},deviceScaleFactor:1});
    page.on('console',message=>{
        report.messages.push({type:message.type(),text:message.text()});
        if(message.type()==='error'||(message.type()==='warning'&&!message.text().startsWith('The powerPreference option is currently ignored')))
            report.errors.push(message.text());
    });
    page.on('pageerror',error=>report.errors.push(error.stack??error.message));
    let heldRoute, announce;
    const requested=new Promise(resolve=>{announce=resolve;});
    await page.route('**/assets/models/laboratory.bin',route=>{heldRoute=route;announce();});
    await page.goto(`${origin}/?ui=0&demo=model`);
    await requested;
    const before=await page.evaluate(()=>Module.frameCount);
    await page.waitForFunction(n=>Module.frameCount>=n+4,before,{timeout:120000});
    assert.equal(await page.evaluate(()=>Module.textureReady),false);
    report.framesDuringDependencyWait=true;
    await heldRoute.continue();await capture(page,'gltf');
    await page.unroute('**/assets/models/laboratory.bin');
    await page.goto(`${origin}/?ui=0&demo=model&asset=assets/models/laboratory.glb`);await capture(page,'glb-embedded-image');
    await page.setViewportSize({width:773,height:650});
    await page.waitForFunction(()=>Module.canvas.width===773);await capture(page,'resized');

    let cancelledRoute, announceCancel;
    const cancelRequested=new Promise(resolve=>{announceCancel=resolve;});
    await page.route('**/assets/models/laboratory.bin',route=>{cancelledRoute=route;announceCancel();});
    await page.goto(`${origin}/?ui=0&demo=model`);await cancelRequested;
    await page.evaluate(()=>{Module.cancelModel=true;});
    await page.waitForFunction(()=>Module.modelCancelled);
    await cancelledRoute.continue().catch(()=>{}); // Fetch abort may already have disposed the intercepted request.
    const cancelFrame=await page.evaluate(()=>Module.frameCount);
    await page.waitForFunction(n=>Module.frameCount>=n+6,cancelFrame);
    assert.equal(await page.evaluate(()=>Module.textureReady),false);
    assert.equal(await page.evaluate(()=>Module.failed),false);
    report.cancelledWithoutLatePublication=true;
    const failurePage=await browser.newPage();
    report.expectedFailureMessages=[];
    failurePage.on('console',message=>report.expectedFailureMessages.push(message.text()));
    failurePage.on('pageerror',error=>report.errors.push(error.stack??error.message));
    const invalid=JSON.parse(await readFile(new URL('../assets/models/laboratory.gltf',import.meta.url),'utf8'));
    invalid.scene=999;
    await failurePage.route('**/assets/models/laboratory.gltf',route=>route.fulfill({contentType:'model/gltf+json',body:JSON.stringify(invalid)}));
    await failurePage.goto(`${origin}/?ui=0&demo=model`);
    await failurePage.waitForFunction(()=>Module.failed,null,{timeout:120000});
    assert.ok(report.expectedFailureMessages.some(message=>message.includes('Invalid glTF default scene index 999')));
    assert.equal(await failurePage.evaluate(()=>Module.textureReady),false);
    report.invalidModelReportedWithoutPublication=true;
    await failurePage.close();
    await page.goto(`${origin}/?demo=model&asset=assets/models/laboratory.glb`);
    await capture(page,'workspace');
    const ui=await page.evaluate(()=>Module.uiState);
    assert.ok(ui.sceneRect[2]>ui.sceneRect[0] && ui.sceneRect[3]>ui.sceneRect[1]);
    report.workspaceModelPresented=true;
    assert.deepEqual(report.errors,[]);report.passed=true;
} catch(error) {report.failure=error.stack??String(error);throw error;}
finally {
    await writeFile(`${artifacts}/report.json`,JSON.stringify(report,null,2));
    await browser?.close();await new Promise(resolve=>server.close(resolve));
}
console.log(`Model smoke passed; ${artifacts}`);
