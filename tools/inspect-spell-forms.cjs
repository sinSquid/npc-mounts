const fs=require('fs'),z=require('zlib');const b=fs.readFileSync(process.argv[2]);
function walk(start,end){for(let p=start;p<end;){const type=b.toString('ascii',p,p+4),size=b.readUInt32LE(p+4);if(type==='GRUP'){walk(p+24,p+size);p+=size;continue;}
if(['ARMO','BOOK'].includes(type)){let d=b.subarray(p+24,p+24+size);if(b.readUInt32LE(p+8)&0x40000)d=z.inflateSync(d.subarray(4));const f=[];for(let q=0;q<d.length;){const tag=d.toString('ascii',q,q+4),n=d.readUInt16LE(q+4);f.push({tag,data:d.subarray(q+6,q+6+n)});q+=6+n;}const ed=f.find(x=>x.tag==='EDID')?.data.toString();if(/Prisoner.*Cuff|Cuff.*Prisoner|SpellTomeParalyze/i.test(ed||''))console.log(type,b.readUInt32LE(p+12).toString(16),ed,JSON.stringify(f.map(x=>[x.tag,x.data.length,['EDID','MODL'].includes(x.tag)?x.data.toString():x.data.toString('hex')])));}
p+=24+size;}}
walk(0,b.length);
