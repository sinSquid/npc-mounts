const fs=require('fs'),assert=require('assert');
function parse(file){
 const b=fs.readFileSync(file),out=new Map();
 function visit(start,end){for(let p=start;p<end;){
  const type=b.toString('ascii',p,p+4),size=b.readUInt32LE(p+4);
  if(type==='GRUP'){visit(p+24,p+size);p+=size;continue;}
  assert(!(b.readUInt32LE(p+8)&0x40000));
  const id=b.readUInt32LE(p+12),header=Buffer.from(b.subarray(p,p+24));header.writeUInt32LE(0,4);
  out.set(`${type}:${id}:header`,header);
  let index=0;for(let q=p+24;q<p+24+size;){const tag=b.toString('ascii',q,q+4),len=b.readUInt16LE(q+4);out.set(`${type}:${id}:${index++}:${tag}`,b.subarray(q+6,q+6+len));q+=6+len;}
  p+=24+size;
 }}visit(0,b.length);return out;
}
const old=parse(process.argv[2]),next=parse(process.argv[3]);
assert.deepEqual([...old.keys()],[...next.keys()]);const changes=[];
for(const [key,value] of old){if(!value.equals(next.get(key))){
 assert(/^(MGEF:16779271:\d+:DNAM|SPEL:16779275:\d+:DESC|BOOK:16779283:\d+:DESC)$/.test(key),key);
 const text=next.get(key).toString('utf8');assert(text.includes('已有坐骑形态')&&text.includes('目标背包'));changes.push(key);
}}
assert.equal(changes.length,3);console.log('PASS: only Capture MGEF/SPEL/BOOK descriptions changed; all IDs, names, records and other fields preserved.',changes);
