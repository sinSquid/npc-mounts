// Generate an independent ESL-flagged ESP. Skyrim.esm is the only master.
const fs=require('fs'),path=require('path'),zlib=require('zlib'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),esm=process.argv[2],out=path.join(repo,'dist');
if(!esm)throw Error('Usage: node tools/build-plugin.cjs <Skyrim.esm>');
const source=fs.readFileSync(esm),templates=[];let eitherHand=0;
function fields(buffer){const result=[];for(let p=0;p<buffer.length;){const tag=buffer.toString('ascii',p,p+4),n=buffer.readUInt16LE(p+4);assert(tag!=='XXXX');result.push({tag,data:buffer.subarray(p+6,p+6+n)});p+=6+n;}return result;}
function visit(start,end){for(let p=start;p<end;){const type=source.toString('ascii',p,p+4),size=source.readUInt32LE(p+4);if(type==='GRUP'){visit(p+24,p+size);p+=size;continue;}
 if(['MGEF','EQUP'].includes(type)){let b=source.subarray(p+24,p+24+size);if(source.readUInt32LE(p+8)&0x40000)b=zlib.inflateSync(b.subarray(4));const f=fields(b),edid=f.find(x=>x.tag==='EDID')?.data.toString('utf8').replace(/\0/g,'');const data=f.find(x=>x.tag==='DATA')?.data;
  if(type==='EQUP'&&edid==='EitherHand')eitherHand=source.readUInt32LE(p+12);
  if(type==='MGEF'&&data?.length===152&&data.readUInt32LE(64)===21&&data.readUInt32LE(80)===1&&data.readUInt32LE(84)===2)templates.push({id:source.readUInt32LE(p+12),edid,data});
 }p+=24+size;}}
visit(0,source.length);assert(templates.length&&eitherHand);
const template=templates.find(x=>x.edid==='ParalyzeFFActor')||templates[0];
const u32=n=>{const b=Buffer.alloc(4);b.writeUInt32LE(n>>>0);return b;},str=s=>Buffer.from(s+'\0','utf8');
function sub(tag,b){const h=Buffer.alloc(6);h.write(tag);h.writeUInt16LE(b.length,4);return Buffer.concat([h,b]);}
function record(type,id,f,flags=0){const data=Buffer.concat(f.map(([tag,b])=>sub(tag,b))),h=Buffer.alloc(24);h.write(type);h.writeUInt32LE(data.length,4);h.writeUInt32LE(flags,8);h.writeUInt32LE(id,12);h.writeUInt16LE(44,20);return Buffer.concat([h,data]);}
function group(type,records){const data=Buffer.concat(records),h=Buffer.alloc(24);h.write('GRUP');h.writeUInt32LE(data.length+24,4);h.write(type,8);return Buffer.concat([h,data]);}
const own=n=>0x01000000+n;
const hedr=Buffer.alloc(12);hedr.writeFloatLE(1.7);hedr.writeUInt32LE(6,4);hedr.writeUInt32LE(0x806,8);
const header=record('TES4',0,[['HEDR',hedr],['CNAM',str('npc-mounts')],['SNAM',str('Human appearance, horse-derived animation, native riding')],['MAST',str('Skyrim.esm')],['DATA',Buffer.alloc(8)]],0x200);
const factions=[['NPCMountsConverted',0x800],['NPCMountsRider',0x801]].map(([name,id])=>record('FACT',own(id),[['EDID',str(name)],['FULL',str(name)],['DATA',u32(1)]]));
function effect(id,name,delivery){const data=Buffer.alloc(152);data.writeUInt32LE((1<<10)|(1<<11)|(1<<26)|(1<<27),0);
 data.writeInt32LE(18,12);data.writeInt32LE(-1,16);data.writeFloatLE(0.1,48);data.writeUInt32LE(1,64);data.writeInt32LE(-1,68);
 if(delivery===2)template.data.copy(data,72,72,76);data.writeUInt32LE(1,80);data.writeUInt32LE(delivery,84);data.writeInt32LE(-1,88);
 template.data.copy(data,92,92,96);data.writeUInt32LE(2,140);
 return record('MGEF',own(id),[['EDID',str(name)],['FULL',str(id===0x802?'人形坐骑化':'解除坐骑化')],['DATA',data],['DNAM',str(id===0x802?'保留原外观，将目标转为四足人形坐骑。':'解除当前骑乘或准星目标的坐骑状态。无目标时恢复全部转换。')]]);}
function spell(id,effect,name,delivery){const spit=Buffer.alloc(36);spit.writeUInt32LE(1|(1<<20)|(1<<21),4);spit.writeFloatLE(0.1,12);spit.writeUInt32LE(1,16);spit.writeUInt32LE(delivery,20);spit.writeFloatLE(delivery===2?2000:0,28);
 const efit=Buffer.alloc(12);efit.writeUInt32LE(1,8);
 return record('SPEL',own(id),[['EDID',str(name)],['OBND',Buffer.alloc(12)],['FULL',str(id===0x804?'A·人形坐骑化':'B·解除坐骑化')],['ETYP',u32(eitherHand)],['DESC',str(id===0x804?'对准人形NPC施放；转换后按交互键骑乘。':'优先解除当前坐骑，否则解除准星目标；无目标时恢复全部。')],['SPIT',spit],['EFID',u32(own(effect))],['EFIT',efit]]);}
const output=Buffer.concat([header,group('FACT',factions),group('MGEF',[effect(0x802,'NPCMountsConvertEffect',2),effect(0x803,'NPCMountsRestoreEffect',0)]),group('SPEL',[spell(0x804,0x802,'NPCMountsConvert',2),spell(0x805,0x803,'NPCMountsRestore',0)])]);
fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'NPCMounts.esp'),output);
fs.mkdirSync(path.join(repo,'reports'),{recursive:true});fs.writeFileSync(path.join(repo,'reports/plugin.json'),JSON.stringify({records:6,master:'Skyrim.esm',esl:true,projectileTemplate:template.edid,projectile:template.data.readUInt32LE(72),eitherHand,bytes:output.length},null,2));
console.log('ESP generated: 6 independent records, no NPC/RACE overrides',output.length);
