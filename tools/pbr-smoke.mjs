// PBR presentation and fly-camera smoke on the required WebGPU host; the C++ numerical suite stays native.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';
import { PNG } from 'pngjs';
import { startWebServer } from './serve-web.mjs';
const artifacts=fileURLToPath(new URL('../artifacts/pbr/browser/',import.meta.url));
await mkdir(artifacts,{recursive:true});
const report={errors:[],messages:[],captures:[]};
const server=await startWebServer(0);
let browser;
// Requires substantial bounded geometry, color variation and opaque presentation; no identical edge-pixel assumption.
function inspect(buffer) {
    const png=PNG.sync.read(buffer);
    // The most frequent RGB value is the flat clear color, even when a close-up sphere covers a corner.
    const counts=new Map();let mode=0,maxCount=0;
    for(let i=0;i<png.data.length;i+=4) {
        const key=(png.data[i]<<16)|(png.data[i+1]<<8)|png.data[i+2];
        const count=(counts.get(key)||0)+1;counts.set(key,count);
        if(count>maxCount) {maxCount=count;mode=key;}
    }
    const first=[mode>>16,(mode>>8)&255,mode&255];let foreground=0,bright=0;
    for(let i=0;i<png.data.length;i+=4) {
        assert.equal(png.data[i+3],255);
        if(first.some((v,c)=>Math.abs(png.data[i+c]-v)>2)) ++foreground;
        if(png.data[i]>180) ++bright;
    }
    assert.ok(foreground>png.width*png.height*.04 && foreground<png.width*png.height*.8);
    assert.ok(bright>100,'Expected illuminated surfaces');
    return {width:png.width,height:png.height,foreground,bright};
}
// Waits for several submitted frames, then saves the actual canvas.
async function capture(page,name) {
    // Resizing clears the canvas before Asyncify finishes the replacement frame; lifetime totals are insufficient.
    const before=await page.evaluate(()=>Module.frameCount);
    await page.waitForFunction(n=>Module.failed || (Module.textureFrames>=3 && Module.frameCount>=n+3),before,{timeout:120000});
    assert.equal(await page.evaluate(()=>Module.failed),false);
    const bytes=await page.locator('#canvas').screenshot({path:`${artifacts}/${name}.png`});
    report.captures.push({name,...inspect(bytes)});return bytes;
}
try {
    browser=await chromium.launch({channel:'chrome',headless:!process.argv.includes('--headed')});
    report.browser=browser.version();
    const page=await browser.newPage({viewport:{width:1000,height:800},deviceScaleFactor:1});
    page.on('console',m=>{report.messages.push({type:m.type(),text:m.text()});
        if(m.type()==='error' || (m.type()==='warning'&&!m.text().startsWith('The powerPreference option is currently ignored'))) report.errors.push(m.text());});
    page.on('pageerror',e=>{report.errors.push(e.stack??e.message);});
    // Observe the device created by RHI, rather than infer its enabled limits from an unrelated adapter query.
    await page.addInitScript(()=>{
        const request=navigator.gpu.requestAdapter.bind(navigator.gpu);
        navigator.gpu.requestAdapter=async (...args)=>{
            const adapter=await request(...args);if(!adapter)return adapter;
            const create=adapter.requestDevice.bind(adapter);
            adapter.requestDevice=async (...args)=>{
                const device=await create(...args);
                window.pbrDevice={vendor:adapter.info.vendor,architecture:adapter.info.architecture,
                    maxSampledTexturesPerShaderStage:device.limits.maxSampledTexturesPerShaderStage,
                    maxSamplersPerShaderStage:device.limits.maxSamplersPerShaderStage,
                    maxUniformBufferBindingSize:device.limits.maxUniformBufferBindingSize,
                    maxTextureDimension2D:device.limits.maxTextureDimension2D,features:[...device.features]};
                return device;
            };return adapter;
        };
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/`,{waitUntil:'load'});
    await capture(page,'overview');report.device=await page.evaluate(()=>window.pbrDevice);
    // Match the native offscreen reference viewport exactly, independent of the header's wrapping.
    await page.setViewportSize({width:960,height:760});
    await page.locator('#canvas').evaluate(canvas=>{canvas.style.flex='none';canvas.style.width='960px';canvas.style.height='640px';});
    const matchBefore=await page.evaluate(()=>Module.frameCount);
    await page.waitForFunction(n=>Module.canvas.width===960 && Module.canvas.height===640 && Module.frameCount>=n+3,matchBefore);
    await capture(page,'matched-960x640');
    await page.locator('#canvas').evaluate(canvas=>{canvas.style.flex='';canvas.style.width='';canvas.style.height='';});
    await page.setViewportSize({width:1000,height:800});
    const initial=await page.evaluate(()=>Module.cameraPosition);
    await page.locator('#canvas').click();
    await page.waitForFunction(()=>document.pointerLockElement===Module.canvas);
    await page.keyboard.down('w');
    await page.waitForFunction(z=>Module.cameraPosition[2]>z+.2,initial[2]);
    await page.keyboard.up('w');
    await page.mouse.move(510,410);
    await capture(page,'fly');
    await page.keyboard.press('Escape');
    await page.waitForFunction(()=>document.pointerLockElement===null);
    await page.keyboard.press('r');
    await page.waitForFunction(p=>Module.cameraPosition.every((v,i)=>v===p[i]),initial);
    await capture(page,'reset');
    await page.keyboard.press('f');
    await page.waitForFunction(()=>Module.cameraPosition[2]===-5);
    await capture(page,'closeup');
    await page.keyboard.press('1');
    const beforeNormal=await page.evaluate(()=>Module.frameCount);
    await page.waitForFunction(n=>Module.frameCount>=n+3,beforeNormal);
    const normals=PNG.sync.read(await capture(page,'normals'));
    const center=(Math.floor(normals.height/2)*normals.width+Math.floor(normals.width/2))*4;
    assert.ok(normals.data[center]>160 && normals.data[center+1]>160 && normals.data[center+2]<30,
        'Front sphere normal must point toward -Z, with varying X/Y across its surface');
    await page.keyboard.press('0');await page.keyboard.press('r');
    await page.locator('#canvas').click();await page.waitForFunction(()=>document.pointerLockElement===Module.canvas);
    await page.keyboard.down('w');
    await page.evaluate(()=>window.dispatchEvent(new Event('blur')));
    await page.waitForFunction(()=>document.pointerLockElement===null && Module.keys.size===0);
    await page.keyboard.up('w');await page.keyboard.press('r');
    await page.setViewportSize({width:773,height:650});
    await page.waitForFunction(()=>Module.canvas.width===773);await capture(page,'resized');
    await page.reload({waitUntil:'load'});await capture(page,'reloaded');
    await page.goto(`http://127.0.0.1:${server.address().port}/?pbr=budget`,{waitUntil:'load'});
    await capture(page,'maximum-layout');
    assert.ok(report.messages.some(m=>m.text.includes('pbr-4095: 16 sampled textures, 13 samplers')));
    report.camera={capture:true,movement:true,release:true,reset:true,closeup:true,blurClears:true};
    assert.deepEqual(report.errors,[]);report.passed=true;
} catch(error) {report.failure=error.stack??String(error);throw error;}
finally {
    await writeFile(`${artifacts}/report.json`,JSON.stringify(report,null,2));
    await browser?.close();await new Promise(resolve=>server.close(resolve));
}
console.log(`PBR smoke passed; ${artifacts}`);
