// Fast shader ABI/uniformity check using the pinned native Slang compiler and real browser WGSL validation.
import assert from 'node:assert/strict';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { chromium } from 'playwright-core';
import { startWebServer } from './serve-web.mjs';
const compiler=process.argv[2] || 'build/native/_deps/slang-src/bin/slangc.exe';
const folder='artifacts/pbr/shaders';await mkdir(folder,{recursive:true});
const modules=['common','brdf','iridescence','material','lighting','mesh'];
let source='';for(const name of modules)source+=`\n#line 1 "pbr/${name}.slang"\n`+await readFile(`shaders/pbr/${name}.slang`,'utf8');
const server=await startWebServer(0);let browser;const report=[];
try {
    browser=await chromium.launch({channel:'chrome',headless:true});const page=await browser.newPage();
    await page.goto(`http://127.0.0.1:${server.address().port}/?demo=checkerboard`);
    for(const [name,defines] of [['core',''],['textured',Array.from({length:15},(_,i)=>`#define SLOT${i} 1\n`).join('')],['unlit','#define UNLIT 1\n']]) {
        const file=`${folder}/${name}.slang`;await writeFile(file,'#define MATERIAL_ANISOTROPY 1\n'+defines+source);
        for(const stage of ['vertex','fragment']) {
            const output=`${folder}/${name}-${stage}.wgsl`;
            execFileSync(compiler,[file,'-target','wgsl','-entry',`${stage}Main`,'-stage',stage,'-o',output]);
            const code=await readFile(output,'utf8');
            if(stage==='vertex') {
                const input=code.match(/struct vertexInput_\d+\s*\{([\s\S]*?)\}/)[1];
                for(const [location,field] of ['position','normal','uv','tangent','uv1','color'].entries())
                    assert.match(input,new RegExp(`@location\\(${location}\\) ${field}_`));
            }
            const messages=await page.evaluate(async code=>{
                const adapter=await navigator.gpu.requestAdapter();const device=await adapter.requestDevice();
                const info=await device.createShaderModule({code}).getCompilationInfo();
                const messages=info.messages.map(m=>({type:m.type,message:m.message,line:m.lineNum}));device.destroy();return messages;
            },code);
            report.push({name,stage,messages});assert.deepEqual(messages,[]);
        }
    }
    console.log('PBR WGSL vertex ABI and derivative-uniformity checks passed.');
} finally {await writeFile(`${folder}/report.json`,JSON.stringify(report,null,2));await browser?.close();await new Promise(resolve=>server.close(resolve));}
