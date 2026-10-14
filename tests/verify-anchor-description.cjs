// Structural delta: no functionality or record identity changes in this ESP update.
const fs=require('fs'),assert=require('assert');
function read(file){const bytes=fs.readFileSync(file),records=[];
 function visit(start,end){for(let p=start;p<end;){const tag=bytes.toString('ascii',p,p+4),n=bytes.readUInt32LE(p+4);if(tag==='GRUP'){visit(p+24,p+n);p+=n;continue;}
 const header=Buffer.from(bytes.subarray(p,p+24));header.writeUInt32LE(0,4);const fields=[];
 for(let q=p+24;q<p+24+n;){const key=bytes.toString('ascii',q,q+4),size=bytes.readUInt16LE(q+4);fields.push({key,data:bytes.subarray(q+6,q+6+size)});q+=6+size;}
 records.push({header,id:bytes.readUInt32LE(p+12),fields});p+=24+n;}}
 visit(0,bytes.length);return records;}
const before=read(process.argv[2]),after=read(process.argv[3]);assert.equal(before.length,after.length);const changed=[];
for(let i=0;i<before.length;i++){const a=before[i],b=after[i];assert(a.header.equals(b.header));assert.equal(a.fields.length,b.fields.length);
 for(let j=0;j<a.fields.length;j++){const x=a.fields[j],y=b.fields[j];assert.equal(x.key,y.key);if(x.data.equals(y.data))continue;
 assert.equal(x.key,a.id===0x01000806?'DNAM':'DESC');assert([0x01000806,0x0100080A,0x01000812].includes(a.id));assert(y.data.toString('utf8').includes('强制传送不拦截'));changed.push(a.id.toString(16));}}
assert.equal(changed.length,3);console.log('PASS: only C effect, spell and tome descriptions changed:',changed);
