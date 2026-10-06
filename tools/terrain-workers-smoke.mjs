// Exercise real dedicated browser workers independently from graphics and suspended application WASM.
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
import {chromium} from 'playwright-core';
import {startWebServer} from './serve-web.mjs';
const server=await startWebServer(0);
const browser=await chromium.launch({channel:'chrome',headless:true});
const report={errors:[]};
try {
    const page=await browser.newPage();
    page.on('pageerror',error=>report.errors.push(error.message));
    await page.route('**/worker-check',route=>route.fulfill({contentType:'text/html',body:'<!doctype html><title>Terrain worker proof</title>'}));
    await page.goto(`http://127.0.0.1:${server.address().port}/worker-check`);
    await page.addScriptTag({url:'/terrain-workers.js'});
    await page.evaluate(()=>{
        window.pool=new TerrainWorkerPool();
        window.packet=(sequence,flags=0)=>{
            const bytes=new Uint8Array(104),view=new DataView(bytes.buffer);
            view.setBigUint64(0,1n,true);view.setBigUint64(8,BigInt(sequence),true);
            view.setBigUint64(40,1n,true);view.setUint32(68,32,true);
            view.setFloat64(72,1024,true);view.setFloat64(80,24,true);view.setFloat64(88,180,true);
            view.setUint32(96,4*1024*1024,true);view.setUint32(100,flags,true);
            return bytes;
        };
        window.heartbeat=0;
        const beat=()=>{++window.heartbeat;requestAnimationFrame(beat);};requestAnimationFrame(beat);
        pool.submit(packet(1,1));pool.submit(packet(2));
    });
    await page.waitForFunction(()=>pool.results.length===1&&heartbeat>=3,null,{timeout:30000});
    report.independent=await page.evaluate(()=>({worker:pool.results[0].worker,outcome:pool.results[0].outcome,
        vertices:pool.results[0].vertices.length,held:pool.workers.find(w=>w.job)?.index,heartbeat}));
    assert.equal(report.independent.outcome,0);assert.ok(report.independent.vertices>0);
    assert.notEqual(report.independent.worker,report.independent.held);
    await page.evaluate(()=>pool.cancel(packet(1)));
    await page.waitForFunction(()=>pool.results.length===2,null,{timeout:30000});
    report.cancellation=await page.evaluate(()=>pool.results[1].outcome);
    assert.equal(report.cancellation,1);
    await page.evaluate(()=>pool.submit(packet(3,2)));
    await page.waitForFunction(()=>pool.results.length===3,null,{timeout:30000});
    report.failure=await page.evaluate(()=>({outcome:pool.results[2].outcome,error:pool.results[2].error}));
    assert.equal(report.failure.outcome,2);assert.ok(report.failure.error.length);
    await page.evaluate(()=>{pool.submit(packet(4,1));pool.release();});
    await page.waitForFunction(()=>pool.results.length===4,null,{timeout:30000});
    assert.equal(await page.evaluate(()=>pool.results[3].outcome),0);
    await page.evaluate(()=>{pool.submit(packet(5,1));pool.stop();});
    assert.equal(await page.evaluate(()=>pool.jobs.size),0);
    assert.deepEqual(report.errors,[]);
} finally {
    await mkdir('artifacts/terrain/browser',{recursive:true});
    await writeFile('artifacts/terrain/browser/workers.json',JSON.stringify(report,null,2));
    await browser.close();await new Promise(resolve=>server.close(resolve));
}
