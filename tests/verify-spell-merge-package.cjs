const fs=require('fs'),path=require('path'),assert=require('assert');
const [before,stage]=process.argv.slice(2);assert(before&&stage);
function records(file){const b=fs.readFileSync(file),out=new Map();function visit(start,end){for(let p=start;p<end;){const type=b.toString('ascii',p,p+4),n=b.readUInt32LE(p+4);if(type==='GRUP'){visit(p+24,p+n);p+=n;continue;}const fields=new Map();for(let q=p+24;q<p+24+n;){const tag=b.toString('ascii',q,q+4),len=b.readUInt16LE(q+4);fields.set(tag,b.subarray(q+6,q+6+len));q+=6+len;assert(q<=p+24+n);}const id=b.readUInt32LE(p+12);assert(!out.has(id));out.set(id,{type,fields,bytes:b.subarray(p,p+24+n)});p+=24+n;assert(p<=end);}}visit(0,b.length);return out;}
const old=records(path.join(before,'NPCMounts.esp')),next=records(path.join(stage,'NPCMounts.esp'));
assert.deepEqual([...old.keys()],[...next.keys()]);assert.equal(next.size,26);
const allowed=new Set([0x802,0x804,0x810,0x806,0x80A,0x812,0x807,0x80B,0x813,0x822,0x823,0x824].map(x=>x+0x01000000));
for(const [id,r]of next)if(!allowed.has(id))assert(r.bytes.equals(old.get(id).bytes),'Unexpected ESP change '+id.toString(16));
const field=(id,name)=>next.get(0x01000000+id).fields.get(name);
for(const [id,spell]of [[0x810,0x804],[0x811,0x805],[0x812,0x80A],[0x813,0x80B],[0x814,0x80C],[0x815,0x80C],[0x824,0x804]]){const data=field(id,'DATA');assert(data[0]&4);assert.equal(data.readUInt32LE(4),0x01000000+spell);}
assert(field(0x80B,'EFID'));
assert.equal(field(0x804,'FULL').toString('utf8').replace(/\0/g,''),'人魅-人形坐骑化');
const b=fs.readFileSync(path.join(stage,'SKSE/Plugins/NPCMounts.ini'),'utf8');
assert(/^UndressOnConvert=0$/m.test(b)&&/跳跃高度系数 1\.0~3\.0/.test(b));
console.log('PASS: ESP preserves FormIDs and unrelated records; capture is active, fixed spell keeps its ID, and INI carries the new jump range. Not game QA.');
