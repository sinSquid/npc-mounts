// Generate an independent ESL-flagged ESP. Skyrim.esm is the only master.
const fs=require('fs'),path=require('path'),zlib=require('zlib'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),esm=process.argv[2],out=process.argv[3]?path.resolve(process.argv[3]):path.join(repo,'dist');
if(!esm)throw Error('Usage: node tools/build-plugin.cjs <Skyrim.esm>');
const source=fs.readFileSync(esm),templates=[];let eitherHand=0,bookTemplate,cuffTemplate;
function fields(buffer){const result=[];for(let p=0;p<buffer.length;){const tag=buffer.toString('ascii',p,p+4),n=buffer.readUInt16LE(p+4);assert(tag!=='XXXX');result.push({tag,data:buffer.subarray(p+6,p+6+n)});p+=6+n;}return result;}
function visit(start,end){for(let p=start;p<end;){const type=source.toString('ascii',p,p+4),size=source.readUInt32LE(p+4);if(type==='GRUP'){visit(p+24,p+size);p+=size;continue;}
 if(['MGEF','EQUP','BOOK','ARMO'].includes(type)){let b=source.subarray(p+24,p+24+size);if(source.readUInt32LE(p+8)&0x40000)b=zlib.inflateSync(b.subarray(4));const f=fields(b),edid=f.find(x=>x.tag==='EDID')?.data.toString('utf8').replace(/\0/g,'');const data=f.find(x=>x.tag==='DATA')?.data;
  if(type==='BOOK'&&edid==='SpellTomeParalyze')bookTemplate=f;
  if(type==='ARMO'&&edid==='PrisonerCuffs')cuffTemplate=f;
  if(type==='EQUP'&&edid==='EitherHand')eitherHand=source.readUInt32LE(p+12);
  if(type==='MGEF'&&data?.length===152&&data.readUInt32LE(64)===21&&data.readUInt32LE(80)===1&&data.readUInt32LE(84)===2)templates.push({id:source.readUInt32LE(p+12),edid,data});
 }p+=24+size;}}
visit(0,source.length);assert(templates.length&&eitherHand&&bookTemplate&&cuffTemplate);
const template=templates.find(x=>x.edid==='ParalyzeFFActor')||templates[0];
const u32=n=>{const b=Buffer.alloc(4);b.writeUInt32LE(n>>>0);return b;},str=s=>Buffer.from(s+'\0','utf8');
function sub(tag,b){const h=Buffer.alloc(6);h.write(tag);h.writeUInt16LE(b.length,4);return Buffer.concat([h,b]);}
function record(type,id,f,flags=0){const data=Buffer.concat(f.map(([tag,b])=>sub(tag,b))),h=Buffer.alloc(24);h.write(type);h.writeUInt32LE(data.length,4);h.writeUInt32LE(flags,8);h.writeUInt32LE(id,12);h.writeUInt16LE(44,20);return Buffer.concat([h,data]);}
function group(type,records){const data=Buffer.concat(records),h=Buffer.alloc(24);h.write('GRUP');h.writeUInt32LE(data.length+24,4);h.write(type,8);return Buffer.concat([h,data]);}
const own=n=>0x01000000+n;
const definitions=[
 {effect:0x802,spell:0x804,key:'Convert',name:'人魅-人形坐骑化',delivery:2,description:'保留原种族和外貌，转换为使用人形爬行动作的坐骑。转换时卸下武器与手持法术；INI可选卸下服装与护甲，解除时恢复本模组实际卸下的装备。'},
 {effect:0x803,spell:0x805,key:'Restore',name:'人魅-解除坐骑化与束缚',delivery:0,description:'优先还原当前坐骑，否则还原准星目标。解除本Mod的固定、束缚和牵引；无目标时还原全部。'},
 {effect:0x806,spell:0x80A,key:'Anchor',name:'人魅-固定NPC',delivery:2,description:'对坐骑形态NPC施放：先完整恢复人形、服装和手持装备，再固定于当前位置并连接固定桩绳索；再次施放解除固定。需要LeashFramework。'},
 {effect:0x807,spell:0x80B,key:'Capture',name:'人魅-捕获',delivery:0,description:'以准星选择任意存活NPC：停止敌对、卸下武器与手持法术并原地捆绑，不生成固定桩。再次施放解除捕获，恢复捕获前装备、法术、AI和敌对关系。'},
 {effect:0x808,spell:0x80C,key:'Leash',name:'人魅-牵引坐骑',delivery:2,description:'切换牵引：无本Mod牵引时添加玩家牵引绳，已有时解除。固定桩绳索同时解除固定并清理道具，保留坐骑化。不覆盖其他Mod的绳索；添加需要LeashFramework。'},
 {effect:0x809,spell:0x80D,key:'Unleash',name:'人魅-解除牵引（旧版兼容）',legacy:true,delivery:2,description:'旧版兼容入口，仅解除本Mod的绳索。已学魔法加载时迁移为人魅-牵引坐骑；旧法术书改为教授合并后的切换魔法。'},
 {effect:0x822,spell:0x823,book:0x824,key:'ConvertSex',merged:true,name:'人魅-人形坐骑化（旧版兼容）',delivery:2,description:'已合并到人魅-人形坐骑化；是否卸装由INI与中文菜单统一控制，默认关闭。'}
];
const hedr=Buffer.alloc(12);hedr.writeFloatLE(1.7);hedr.writeUInt32LE(25,4);hedr.writeUInt32LE(0x825,8);
const header=record('TES4',0,[['HEDR',hedr],['CNAM',str('npc-mounts')],['SNAM',str('Humanoid crawl mounts, spell tomes, restraints and optional LeashFramework')],['MAST',str('Skyrim.esm')],['DATA',Buffer.alloc(8)]],0x200);
const factions=[['NPCMountsConverted',0x800],['NPCMountsRider',0x801]].map(([name,id])=>record('FACT',own(id),[['EDID',str(name)],['FULL',str(name)],['DATA',u32(1)]]));
function effect(def){const {effect:id,key,name,delivery,description}=def;const data=Buffer.alloc(152);data.writeUInt32LE((1<<10)|(1<<11)|(1<<26)|(1<<27),0);
 data.writeInt32LE(18,12);data.writeInt32LE(-1,16);data.writeFloatLE(0.1,48);data.writeUInt32LE(1,64);data.writeInt32LE(-1,68);
 if(delivery===2)template.data.copy(data,72,72,76);data.writeUInt32LE(1,80);data.writeUInt32LE(delivery,84);data.writeInt32LE(-1,88);
 template.data.copy(data,92,92,96);data.writeUInt32LE(2,140);
 return record('MGEF',own(id),[['EDID',str('NPCMounts'+key+'Effect')],['FULL',str(name)],['DATA',data],['DNAM',str(description)]]);}
function spell(def){const {spell:id,effect,key,name,delivery,description}=def;const spit=Buffer.alloc(36);spit.writeUInt32LE(1|(1<<20)|(1<<21),4);spit.writeFloatLE(0.1,12);spit.writeUInt32LE(1,16);spit.writeUInt32LE(delivery,20);spit.writeFloatLE(delivery===2?2000:0,28);
 const efit=Buffer.alloc(12);efit.writeUInt32LE(1,8);
 return record('SPEL',own(id),[['EDID',str('NPCMounts'+key)],['OBND',Buffer.alloc(12)],['FULL',str(name)],['ETYP',u32(eitherHand)],['DESC',str(description)],['SPIT',spit],...(def.retired?[]:[['EFID',u32(own(effect))],['EFIT',efit]])]);}
const books=definitions.map((original,i)=>{const redirect=original.legacy?definitions.find(d=>d.key==='Leash'):original.merged?definitions.find(d=>d.key==='Convert'):null;
const d=redirect?{...redirect,key:original.key,name:redirect.name+'（旧版兼容书）'}:original;return record('BOOK',own(original.book??(0x810+i)),bookTemplate.map(f=>{
 let data=Buffer.from(f.data);if(f.tag==='EDID')data=str('NPCMountsTome'+d.key);if(f.tag==='FULL')data=str('法术书：'+d.name);if(f.tag==='DESC')data=str(d.description);
 if(f.tag==='DATA'){assert(data.length===16);if(original.retired){data[0]&=~4;data.writeUInt32LE(0,4);}else data.writeUInt32LE(own(d.spell),4);data.writeUInt32LE(50,8);}return[f.tag,data];
}));});
const cuff=record('ARMO',own(0x821),cuffTemplate.filter(f=>!['MO2T','MO4T','VMAD'].includes(f.tag)).map(f=>[f.tag,f.tag==='EDID'?str('NPCMountsBindingCuffs'):f.tag==='FULL'?str('坐骑束缚绳环'):f.tag==='DESC'?str('由NPC坐骑法术生成；人魅-解除坐骑化与束缚解除并归还原手部装备。'):f.data]));
const post=record('STAT',own(0x820),[['EDID',str('NPCMountsRestraintPost')],['OBND',Buffer.alloc(12)],['MODL',str('NPCMounts\\restraintpost_static.nif')],['DNAM',Buffer.alloc(8)]]);
const output=Buffer.concat([header,group('FACT',factions),group('MGEF',definitions.map(effect)),group('SPEL',definitions.map(spell)),group('BOOK',books),group('STAT',[post]),group('ARMO',[cuff])]);
fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'NPCMounts.esp'),output);
fs.mkdirSync(path.join(repo,'reports'),{recursive:true});fs.writeFileSync(path.join(repo,'reports/plugin.json'),JSON.stringify({records:25,master:'Skyrim.esm',esl:true,spells:definitions.map((d,i)=>({...d,book:d.book??(0x810+i)})),projectileTemplate:template.edid,projectile:template.data.readUInt32LE(72),eitherHand,bytes:output.length},null,2));
console.log('ESP generated: 25 independent records, 5 current spells plus merged compatibility records, no NPC/RACE overrides',output.length);
