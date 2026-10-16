// Source/float32 model checks only. Does not compile or execute the C++ plugin.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),header=fs.readFileSync(path.join(root,'src/Config.h'),'utf8'),cpp=fs.readFileSync(path.join(root,'src/Config.cpp'),'utf8');
const expected={NPCScale:[.5,3,.05],SpeedMultiplier:[1,2,.05],DismountHoldSeconds:[.25,2,.05],DismountAnimationSeconds:[.4,2,.05],MountAnimationSeconds:[.4,2,.05],JumpHeightMultiplier:[.8,2,.1],SeatHeight:[0,60,.5],SeatForward:[-50,50,.5],SeatSide:[-30,30,.5],HeadingDegrees:[-180,180,1],RunThreshold:[40,600,5],SprintThreshold:[80,1200,5]};
const entries=[...header.matchAll(/\{"(\w+)",\s*"[^"]+",\s*&Data::\w+,\s*([\d.\-]+)f,\s*([\d.\-]+)f,\s*([\d.\-]+)f\}/g)];assert.equal(entries.length,12);
const f32=Math.fround;
for(const [,key,...numbers]of entries){const [lo,hi,step]=numbers.map(Number);assert.deepEqual([lo,hi,step],expected[key]);assert(step>=.05);assert(Math.abs((hi-lo)/step-Math.round((hi-lo)/step))<1e-7);
 const snap=value=>{const minimum=Math.round(f32(lo)*100)/100,unit=Math.round(f32(step)*100)/100;const v=Math.max(f32(lo),Math.min(f32(hi),f32(value)));return Math.max(f32(lo),Math.min(f32(hi),f32(minimum+Math.floor((v-minimum)/unit+.5+1e-5)*unit)));};
 assert.equal(snap(lo),f32(lo));assert.equal(snap(hi),f32(hi));for(let i=0;i<=1000;i++){let v=snap(lo+(hi-lo)*i/1000);assert.equal(snap(v),v);assert(v>=f32(lo)&&v<=f32(hi));assert(Math.abs((v-lo)/step-Math.round((v-lo)/step))<.001);}
}
assert(cpp.includes('Normalize(draft);'));assert(cpp.includes('f.step'));assert(!cpp.includes('draft.jumpHeightMultiplier=std::round'));
assert(header.includes('n = Snap(std::isfinite(n) ? n : defaults.*field.member,field);'));
console.log('PASS: 12 unchanged ranges, shared step metadata/menu normalization and float32 grid model; 12,012 samples. No C++ build or runtime test performed.');
