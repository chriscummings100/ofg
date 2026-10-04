// Rebuilds OFG's original glTF/GLB fixture: textured cubes with retained skin, morph and animation data.
import {mkdir, readFile, writeFile} from 'node:fs/promises';
const directory = new URL('../assets/models/', import.meta.url);
await mkdir(directory, {recursive:true});
const chunks=[], views=[], accessors=[]; let byteLength=0;
// Appends a tightly packed float accessor with four-byte-aligned buffer storage.
function accessor(values,type,width) {
    const bytes=Buffer.alloc(values.length*4); values.forEach((v,i)=>bytes.writeFloatLE(v,i*4));
    const view=views.length; views.push({buffer:0,byteOffset:byteLength,byteLength:bytes.length});
    chunks.push(bytes); byteLength+=bytes.length;
    const index=accessors.length; accessors.push({bufferView:view,componentType:5126,count:values.length/width,type});
    return index;
}
const faces=[
    [[-.5,-.5,.5],[.5,-.5,.5],[.5,.5,.5],[-.5,.5,.5]],
    [[.5,-.5,-.5],[-.5,-.5,-.5],[-.5,.5,-.5],[.5,.5,-.5]],
    [[.5,-.5,.5],[.5,-.5,-.5],[.5,.5,-.5],[.5,.5,.5]],
    [[-.5,-.5,-.5],[-.5,-.5,.5],[-.5,.5,.5],[-.5,.5,-.5]],
    [[-.5,.5,.5],[.5,.5,.5],[.5,.5,-.5],[-.5,.5,-.5]],
    [[-.5,-.5,-.5],[.5,-.5,-.5],[.5,-.5,.5],[-.5,-.5,.5]]];
const positions=faces.flat(2), uv=faces.flatMap(()=>[0,1,1,1,1,0,0,0]);
const position=accessor(positions,'VEC3',3);accessors[position].min=[-.5,-.5,-.5];accessors[position].max=[.5,.5,.5];
const texcoord=accessor(uv,'VEC2',2), morph=accessor(Array.from({length:24},()=>[0,.2,.1]).flat(),'VEC3',3);
accessors[morph].min=[0,.2,.1];accessors[morph].max=[0,.2,.1];
const weights=accessor(Array.from({length:24},()=>[1,0,0,0]).flat(),'VEC4',4);
const joints=accessors.length; const jointBytes=Buffer.alloc(24*4);
accessors.push({bufferView:views.length,componentType:5121,count:24,type:'VEC4'});
views.push({buffer:0,byteOffset:byteLength,byteLength:jointBytes.length});chunks.push(jointBytes);byteLength+=jointBytes.length;
const indices=accessors.length, indexBytes=Buffer.from(faces.flatMap((_,i)=>[0,1,2,0,2,3].map(v=>v+i*4)));
accessors.push({bufferView:views.length,componentType:5121,count:36,type:'SCALAR'});
views.push({buffer:0,byteOffset:byteLength,byteLength:indexBytes.length});chunks.push(indexBytes);byteLength+=indexBytes.length;
const times=accessor([0,1],'SCALAR',1);accessors[times].min=[0];accessors[times].max=[1];
const translation=accessor([0,0,0,0,0,1],'VEC3',3);
const rotation=accessor([0,0,0,0, 0,0,0,1, 1,2,3,4, 4,3,2,1, 0,0,0,1, 0,0,0,0],'VEC4',4);
const morphKeys=accessor([0,1],'SCALAR',1);
const buffer=Buffer.concat(chunks);
const document={asset:{version:'2.0',generator:'OFG original model fixture'},
    buffers:[{uri:'laboratory.bin',byteLength}],bufferViews:views,accessors,
    images:[{uri:'../checker.png'}],samplers:[{wrapS:33648,wrapT:10497,minFilter:9729,magFilter:9729}],textures:[{source:0,sampler:0}],
    materials:[{pbrMetallicRoughness:{baseColorFactor:[.7,.85,1,1],metallicFactor:.1,roughnessFactor:.6,baseColorTexture:{index:0}},doubleSided:false}],
    meshes:[{name:'Cube with retained deformation data',primitives:[{attributes:{POSITION:position,TEXCOORD_0:texcoord,JOINTS_0:joints,WEIGHTS_0:weights},indices,material:0,targets:[{POSITION:morph}]}],weights:[.1]}],
    nodes:[{name:'Left cube',mesh:0,skin:0,weights:[.25],translation:[-.8,0,0]},{name:'Joint'},{name:'Model root',children:[0,1,3]},
        {name:'Right cube',mesh:0,skin:0,weights:[.75],translation:[.8,0,0]}],skins:[{name:'One joint skin',joints:[1],skeleton:1}],
    animations:[{name:'Translate',samplers:[{input:times,output:translation,interpolation:'STEP'}],channels:[{sampler:0,target:{node:1,path:'translation'}}]},
        {name:'Rotate',samplers:[{input:times,output:rotation,interpolation:'CUBICSPLINE'}],channels:[{sampler:0,target:{node:1,path:'rotation'}}]},
        {name:'Morph',samplers:[{input:times,output:morphKeys,interpolation:'LINEAR'}],channels:[{sampler:0,target:{node:0,path:'weights'}}]}],
    scenes:[{name:'Cubes',nodes:[2]},{name:'Empty'}],scene:0};
await writeFile(new URL('laboratory.bin',directory),buffer);
await writeFile(new URL('laboratory.gltf',directory),JSON.stringify(document,null,2)+'\n');
delete document.buffers[0].uri;
const png=await readFile(new URL('../assets/checker.png',import.meta.url));
document.images=[{bufferView:views.length,mimeType:'image/png'}];
views.push({buffer:0,byteOffset:buffer.length,byteLength:png.length});
const glbBuffer=Buffer.concat([buffer,png,Buffer.alloc((4-png.length%4)%4)]);
document.buffers[0].byteLength=glbBuffer.length;
let json=Buffer.from(JSON.stringify(document));json=Buffer.concat([json,Buffer.alloc((4-json.length%4)%4,32)]);
const header=Buffer.alloc(20);header.writeUInt32LE(0x46546c67);header.writeUInt32LE(2,4);header.writeUInt32LE(28+json.length+glbBuffer.length,8);
header.writeUInt32LE(json.length,12);header.writeUInt32LE(0x4e4f534a,16);
const binHeader=Buffer.alloc(8);binHeader.writeUInt32LE(glbBuffer.length);binHeader.writeUInt32LE(0x004e4942,4);
await writeFile(new URL('laboratory.glb',directory),Buffer.concat([header,json,binHeader,glbBuffer]));
console.log('Generated original glTF/GLB model fixtures.');
