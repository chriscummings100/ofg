import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';
import { PNG } from 'pngjs';
import { startWebServer } from './serve-web.mjs';
// Outdoor runtime proof on an actual device restricted to the portable sampled-texture baseline.
const directory=fileURLToPath(new URL('../artifacts/lighting/browser/',import.meta.url));
await mkdir(directory,{recursive:true});
const report={errors:[],messages:[],captures:[],updates:{}},server=await startWebServer(0);
let browser;
try {
    browser=await chromium.launch({channel:'chrome',headless:true});
    report.browser=browser.version();
    const page=await browser.newPage({viewport:{width:1000,height:800},deviceScaleFactor:1});
    page.on('console',message=>{
        const text=message.text();report.messages.push(text);
        if(message.type()==='error' || (message.type()==='warning'&&!text.startsWith('The powerPreference option is currently ignored')))report.errors.push(text);
    });
    page.on('pageerror',e=>report.errors.push(e.stack??e.message));
    await page.addInitScript(()=>{
        window.iblHistory=[];
        let previous='';
        const record=()=>{
            const update=window.Module?.iblUpdate;
            if(update) {
                const key=JSON.stringify(update);
                if(key!==previous)window.iblHistory.push({...update,time:performance.now()});
                previous=key;
            }
            requestAnimationFrame(record);
        };
        requestAnimationFrame(record);
        const request=navigator.gpu.requestAdapter.bind(navigator.gpu);
        navigator.gpu.requestAdapter=async (...args)=>{
            const adapter=await request(...args);if(!adapter)return adapter;
            const create=adapter.requestDevice.bind(adapter);
            adapter.requestDevice=async (desc={})=>{
                desc.requiredLimits={...desc.requiredLimits,maxSampledTexturesPerShaderStage:16,maxSamplersPerShaderStage:16};
                const device=await create(desc);
                window.lightingDevice={vendor:adapter.info.vendor,architecture:adapter.info.architecture,
                    textures:device.limits.maxSampledTexturesPerShaderStage,samplers:device.limits.maxSamplersPerShaderStage};
                device.addEventListener('uncapturederror',e=>console.error(e.error.message));
                return device;
            };return adapter;
        };
    });
    for(const [name,query] of [['noon','hour=12'],['sunset','hour=17.9'],['twilight','hour=18.5'],['midnight','hour=0'],['overcast','hour=12&clouds=.95'],['settings','hour=12&ui=1']]) {
        if(name==='settings')await page.setViewportSize({width:1440,height:940});
        await page.goto(`http://127.0.0.1:${server.address().port}/?demo=outdoor&${query}${name==='settings' ? '' : '&ui=0'}`,{waitUntil:'load'});
        if(name==='noon') {
            await page.waitForFunction(()=>Module.failed||Module.iblUpdate?.steps>=3,null,{timeout:180000});
            await page.locator('#canvas').screenshot({path:`${directory}/startup-capture.png`});
        }
        await page.waitForFunction(()=>Module.failed||Module.iblUpdate?.publications>=1,null,{timeout:180000});
        assert.equal(await page.evaluate(()=>Module.failed),false,report.errors.join('\n'));
        const updates=await page.evaluate(()=>window.iblHistory);
        assert.ok(updates.some(s=>s.publications===0&&s.steps>0&&s.steps<6),'Startup capture must span frames');
        assert.ok(updates.some(s=>s.publications===0&&s.steps>6),'Filtering must span frames before publication');
        assert.ok(updates.every(s=>s.passes===0||s.passes===1||s.passes===7),'IBL work must remain bounded');
        report.updates[name]=updates;
        const bytes=await page.locator('#canvas').screenshot({path:`${directory}/${name}.png`});
        const png=PNG.sync.read(bytes);let sum=0,white=0,black=0;
        for(let i=0;i<png.data.length;i+=4){const v=png.data[i]+png.data[i+1]+png.data[i+2];sum+=v;white+=v>750;black+=v<3;}
        const pixels=png.width*png.height;
        assert.ok(black<pixels*.9 && white<pixels*.8,`${name}: meaningful lighting range`);
        report.captures.push({name,width:png.width,height:png.height,mean:sum/pixels/3,white,black});
        report.device=await page.evaluate(()=>window.lightingDevice);
        if(name==='settings') {
            // Coordinates come from the inspected 1440x901 canvas; exercise real ImGui preset and clock input.
            const canvas=await page.locator('#canvas').boundingBox();
            // Compare haze on/off through the real settings checkbox; toggling composition must not rebake IBL.
            const hazePublications=await page.evaluate(()=>Module.iblUpdate.publications);
            await page.mouse.click(canvas.x+1125,canvas.y+716);
            const hazeFrame=await page.evaluate(()=>Module.frameCount);
            await page.waitForFunction(n=>Module.failed || Module.frameCount>=n+3,hazeFrame,{timeout:60000});
            const withoutHaze=PNG.sync.read(await page.locator('#canvas').screenshot({path:`${directory}/haze-off.png`}));
            assert.equal(await page.evaluate(()=>Module.iblUpdate.publications),hazePublications);
            let foregroundDifference=0, foregroundSamples=0;
            for(let y=700;y<800;y+=3)for(let x=400;x<1000;x+=3) {
                const i=(y*withoutHaze.width+x)*4;
                for(let c=0;c<3;c++)foregroundDifference+=Math.abs(withoutHaze.data[i+c]-png.data[i+c]);
                foregroundSamples+=3;
            }
            report.foregroundHazeDifference=foregroundDifference/foregroundSamples;
            assert.ok(report.foregroundHazeDifference<3,'Metres of foreground air must not substantially brighten the ground');
            await page.mouse.click(canvas.x+1125,canvas.y+716);
            const publications=await page.evaluate(()=>Module.iblUpdate.publications);
            await page.mouse.click(canvas.x+1320,canvas.y+491);
            await page.waitForFunction(n=>Module.failed || (Module.iblUpdate.publications===n && Module.iblUpdate.steps>=3),publications,{timeout:60000});
            const pending=PNG.sync.read(await page.locator('#canvas').screenshot({path:`${directory}/ui-midnight-updating.png`}));
            let whitePending=0, pendingSamples=0;
            for(let y=300;y<800;y+=5)for(let x=350;x<1090;x+=5) {
                const i=(y*pending.width+x)*4;
                whitePending+=pending.data[i]+pending.data[i+1]+pending.data[i+2]>750;
                ++pendingSamples;
            }
            assert.ok(whitePending<pendingSamples*.1,'Pending daylight IBL must not blow out under midnight exposure');
            await page.waitForFunction(n=>Module.failed || Module.iblUpdate.publications>n,publications,{timeout:60000});
            const night=PNG.sync.read(await page.locator('#canvas').screenshot({path:`${directory}/ui-midnight.png`}));
            let difference=0;
            for(let y=50;y<800;y+=5)for(let x=350;x<1090;x+=5) {
                const i=(y*night.width+x)*4;difference+=Math.abs(night.data[i]-png.data[i]);
            }
            assert.ok(difference>10000,'Midnight preset must update the rendered scene');
            await page.mouse.click(canvas.x+1125,canvas.y+400);
            const running=await page.evaluate(()=>Module.iblUpdate.publications);
            await page.waitForFunction(n=>Module.failed || Module.iblUpdate.publications>=n+2,running,{timeout:90000});
            assert.equal(await page.evaluate(()=>Module.failed),false);
            report.runningUpdates=await page.evaluate(()=>window.iblHistory);
            assert.ok(report.runningUpdates.every(s=>s.passes<=7));
            await page.locator('#canvas').screenshot({path:`${directory}/ui-running-cycle.png`});
        }

        if(name==='noon') {
            const before=await page.evaluate(()=>({time:performance.now(),frame:Module.frameCount}));
            await page.waitForFunction(n=>Module.frameCount>=n+30,before.frame,{timeout:60000});
            report.pausedFrameIntervalMs=await page.evaluate(before=>(performance.now()-before.time)/(Module.frameCount-before.frame),before);
            const original=await page.evaluate(()=>Module.cameraPosition);
            await page.locator('#canvas').click();
            await page.waitForFunction(()=>document.pointerLockElement===Module.canvas);
            await page.keyboard.down('w');
            await page.waitForFunction(z=>Module.cameraPosition[2]>z+.3,original[2]);
            await page.keyboard.up('w');
            await page.locator('#canvas').screenshot({path:`${directory}/fly.png`});
            await page.keyboard.press('Escape');
            await page.waitForFunction(()=>document.pointerLockElement===null);
            await page.setViewportSize({width:773,height:650});
            const old=await page.evaluate(()=>Module.frameCount);
            await page.waitForFunction(n=>Module.frameCount>=n+3 && Module.canvas.width===773,old);
            await page.locator('#canvas').screenshot({path:`${directory}/resized.png`});
            await page.keyboard.press('r');
            await page.waitForFunction(()=>Module.cameraPosition[1]===3);
        }
    }
    assert.ok(Math.abs(report.captures.find(c=>c.name==='noon').mean-report.captures.find(c=>c.name==='midnight').mean)>10,"Noon and midnight must differ");
    assert.equal(report.device.textures,16);
    assert.deepEqual(report.errors,[]);
} finally {
    await writeFile(`${directory}/report.json`,JSON.stringify(report,null,2));
    await browser?.close();await new Promise(resolve=>server.close(resolve));
}
console.log('Outdoor WebGPU smoke passed.');
