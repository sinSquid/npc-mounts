const fs=require('fs'),assert=require('assert');
const db=fs.readFileSync('E:/iniRePather-Skyrim/Mod Organizer 2/mods/插件地址库-Address Library for SKSE Plugins All in one/SKSE/Plugins/version-1-5-97-0.bin');
let at=0;function read(n){let v=n===8?Number(db.readBigUInt64LE(at)):db.readUIntLE(at,n);at+=n;return v;}
assert.equal(read(4),1);at+=16;const nameLength=read(4);at+=nameLength;const ptr=read(4),count=read(4);assert.equal(ptr,8);
function delta(t,prev){switch(t){case 0:return read(8);case 1:return prev+1;case 2:return prev+read(1);case 3:return prev-read(1);case 4:return prev+read(2);case 5:return prev-read(2);case 6:return read(2);case 7:return read(4);default:throw Error('bad type');}}
const ids=new Map();let id=0,offset=0;
for(let i=0;i<count;i++){const type=read(1);id=delta(type&15,id);const hi=type>>4;offset=delta(hi&7,hi&8?offset/ptr:offset)*(hi&8?ptr:1);ids.set(id,offset);}
assert.equal(at,db.length);
const exe=fs.readFileSync('E:/iniRePather-Skyrim/Mod Organizer 2/Stock Game/SkyrimSE.exe');
const pe=exe.readUInt32LE(60),sections=pe+24+exe.readUInt16LE(pe+20),n=exe.readUInt16LE(pe+6);
function bytes(rva,size){for(let i=0;i<n;i++){const p=sections+i*40,start=exe.readUInt32LE(p+12);if(rva>=start&&rva+size<=start+exe.readUInt32LE(p+16))return exe.subarray(exe.readUInt32LE(p+20)+rva-start,exe.readUInt32LE(p+20)+rva-start+size);}throw Error('no section');}
const movement=ids.get(36359),cameraTable=ids.get(256647);
console.log('Movement caller RVA',movement.toString(16),'call',bytes(movement+0xf0,5).toString('hex'));
const call=bytes(movement+0xf0,5);assert.equal(call[0],0xe8);
const target=movement+0xf0+5+call.readInt32LE(1);console.log('Movement target RVA',target.toString(16));
const camera=Number(bytes(cameraTable+0xd*8,8).readBigUInt64LE())-0x140000000;
console.log('Camera table RVA',cameraTable.toString(16),'SetFreeRotationMode RVA',camera.toString(16));
assert.equal(movement,0x5d8170);assert.equal(target,0x5d87f0);assert.equal(camera,0x84fa20);
assert.equal(bytes(movement+0xea,11).toString('hex'),'0f28cf488bcee88b050000');
assert.equal(bytes(target+0x65,7).toString('hex'),'440f28c9488bf9'); // XMM1 delta / RCX actor.
assert.equal(bytes(camera+0x19,6).toString('hex'),'0fb6f2488bd9'); // DL bool / RCX camera.
console.log('PASS: installed caller/callee registers agree with Actor*,float and ThirdPersonState*,bool signatures. Not game QA.');
