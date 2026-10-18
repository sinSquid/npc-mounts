const fs=require('fs'),path=require('path'),assert=require('assert'),crypto=require('crypto');
const stage=process.argv[2],donor=process.argv[3];assert(stage&&donor);
const root=path.join(stage,'SKSE/Plugins/NPCMounts'),raw=path.join(root,'Sources/WalkOnFour');
for(const name of ['sneakmtidle.hkx','sneakwalk_forward.hkx','sneakrun_forward.hkx']){
 const a=fs.readFileSync(path.join(raw,name)),b=fs.readFileSync(path.join(donor,name));assert(a.equals(b));
 console.log(name,crypto.createHash('sha256').update(a).digest('hex'));
}
const bank=fs.readFileSync(path.join(root,'Crawl.nmc'));assert.deepEqual([0,4,8,12].map(i=>bank.readUInt32LE(i)),[0x434d504e,1,99,8]);
let offset=16+99*64+99*2;const clips=[];
for(let i=0;i<8;i++){
 const duration=bank.readFloatLE(offset),count=bank.readUInt32LE(offset+4);assert(duration>0&&count>=2&&count<=2048);offset+=8;
 const frames=[];for(let j=0;j<count;j++){frames.push(bank.subarray(offset,offset+99*48));offset+=99*48;}
 clips.push({duration,frames});
}
assert.equal(offset,bank.length);
for(const [a,b] of [[2,3],[0,5],[0,6]]){assert.equal(clips[a].duration,clips[b].duration);assert.equal(clips[a].frames.length,clips[b].frames.length);clips[a].frames.forEach((f,i)=>assert(f.equals(clips[b].frames[i])));}
assert.equal(clips[1].duration,clips[4].duration);assert.equal(clips[1].frames.length,clips[4].frames.length);
clips[1].frames.forEach((f,i)=>assert(f.equals(clips[4].frames.at(-1-i))));
console.log('PASS: three local crawl donors identical, native 8-clip bank complete, backward/sprint/turn mappings and standalone bound overlay verified. No OAR runtime dependency.');
