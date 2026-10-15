const fs=require('fs'),path=require('path'),assert=require('assert'),z=require('zlib'),crypto=require('crypto');
const repo=path.resolve(__dirname,'..'),dist=process.argv[2]?path.resolve(process.argv[2]):path.join(repo,'dist'),records=[];
function parse(b,start=0,end=b.length){for(let p=start;p<end;){const type=b.toString('ascii',p,p+4),n=b.readUInt32LE(p+4);if(type==='GRUP'){parse(b,p+24,p+n);p+=n;continue;}let data=b.subarray(p+24,p+24+n);if(b.readUInt32LE(p+8)&0x40000)data=z.inflateSync(data.subarray(4));const fields=[];for(let q=0;q<data.length;){const tag=data.toString('ascii',q,q+4),len=data.readUInt16LE(q+4);fields.push({tag,data:data.subarray(q+6,q+6+len)});q+=6+len;assert(q<=data.length);}records.push({type,id:b.readUInt32LE(p+12),flags:b.readUInt32LE(p+8),fields});p+=24+n;assert(p<=end);}}
parse(fs.readFileSync(path.join(dist,'NPCMounts.esp')));
assert.equal(records.length,23);assert(records[0].flags&0x200);
const names=['人魅-人形坐骑化','人魅-解除坐骑化与束缚','人魅-固定坐骑','人魅-束缚敌对目标','人魅-牵引坐骑','人魅-解除牵引'];
for(const type of ['SPEL','MGEF','BOOK']){
 const fulls=records.filter(r=>r.type===type).map(r=>r.fields.find(f=>f.tag==='FULL').data.toString('utf8').replace(/\0/g,'').replace(/^法术书：/,''));
 assert.deepEqual(fulls,names);
}
for(const r of records)for(const f of r.fields)if(['FULL','DESC','DNAM','SNAM'].includes(f.tag)&&r.type!=='STAT')
 assert(!/[A-F](?:[·～]|可|解除|还原|只对|需要)/.test(f.data.toString('utf8')),'Legacy spell name in '+r.type);
assert.deepEqual(records[0].fields.filter(x=>x.tag==='MAST').map(x=>x.data.toString().replace(/\0/g,'')),['Skyrim.esm']);
assert(!records.some(r=>['NPC_','RACE','HDPT','TXST'].includes(r.type)));
assert.deepEqual(records.filter(r=>r.type==='ARMO').map(r=>r.id),[0x01000821]);
for(let id=0x800;id<=0x805;id++)assert(records.some(r=>r.id===0x01000000+id));
for(const r of records.filter(r=>r.type==='MGEF')){
 const data=r.fields.find(x=>x.tag==='DATA').data;
 assert.equal(data.length,152);assert.equal(data.readUInt32LE(64),1);assert(!(data.readUInt32LE(0)&5));
 assert.equal(data.readUInt32LE(80),1);assert.equal(data.readUInt32LE(84),r.id===0x01000803?0:2);
}
for(const r of records.filter(r=>r.type==='SPEL')){
 const ref=r.fields.find(x=>x.tag==='EFID').data.readUInt32LE();assert(records.some(v=>v.id===ref&&v.type==='MGEF'));
 assert.equal(r.fields.find(x=>x.tag==='SPIT').data.length,36);
}
function files(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>e.isDirectory()?files(path.join(root,e.name)):[path.join(root,e.name)]);}
const animationReport=JSON.parse(fs.readFileSync(path.join(repo,'reports/animations.json'))),hashes=new Set(animationReport.clips.map(x=>x.sha256));
assert.equal(animationReport.clips.length,18);assert(animationReport.clips.every(x=>x.tracks===99&&x.sdkDecode));
const all=files(dist),jsons=all.filter(f=>f.endsWith('config.json'));assert.equal(jsons.length,12);
const ranks=new Set();let hkx=0;
for(const filename of all){
 if(filename.endsWith('.hkx')){const b=fs.readFileSync(filename);assert.equal(b[16],8);assert(hashes.has(crypto.createHash('sha256').update(b).digest('hex')));hkx++;}
 if(filename.endsWith('config.json')){
  const cfg=JSON.parse(fs.readFileSync(filename));if(!cfg.conditions)continue;
  assert(cfg.interruptible);assert(cfg.priority<2147483647);
  const c=cfg.conditions[0];assert.equal(c.Faction.pluginName,'NPCMounts.esp');
  if(c.condition==='FactionRank'){assert.equal(c.Comparison,'==');assert.equal(c.Faction.formID,'800');ranks.add(c['Numeric value'].value);}
  else{assert.equal(c.condition,'IsInFaction');assert.equal(c.Faction.formID,'801');}
 }
}
assert.deepEqual([...ranks].sort((a,b)=>a-b),[0,1,2,3,4,5,6,7,8,9]);
assert.equal(hkx,animationReport.aliases.length*22);
for(let actorRank=-1;actorRank<=9;actorRank++)assert.equal([...ranks].filter(r=>r===actorRank).length,actorRank<0?0:1);
assert(fs.statSync(path.join(dist,'SKSE/Plugins/NPCMounts.dll')).size>100000);
const motion=fs.readFileSync(path.join(dist,'SKSE/Plugins/NPCMounts/Dismount.nmp'));
assert.deepEqual([0,4,8,12].map(i=>motion.readUInt32LE(i)),[0x504d504e,1,99,31]);
assert.equal(motion.length,16+99*64+99*2+31*99*48);
const crawl=fs.readFileSync(path.join(dist,'SKSE/Plugins/NPCMounts/Crawl.nmc'));
assert.deepEqual([0,4,8,12].map(i=>crawl.readUInt32LE(i)),[0x434d504e,1,99,7]);
let crawlOffset=16+99*64+99*2;for(let i=0;i<7;i++){assert(crawl.readFloatLE(crawlOffset)>0);const frames=crawl.readUInt32LE(crawlOffset+4);assert(frames>=2&&frames<=2048);crawlOffset+=8+frames*99*48;}assert.equal(crawlOffset,crawl.length);
assert(fs.readFileSync(path.join(dist,'SKSE/Plugins/NPCMounts.ini'),'utf8').includes('[Mounts]'));
assert.equal(animationReport.sourceMode,'human-crawl');
const implementation=fs.readFileSync(path.join(repo,'src/plugin.cpp'),'utf8');
assert(!/\bSetRace\s*\(|\brace\s*=|\bSetActorBase\s*\(/.test(implementation));
const books=records.filter(r=>r.type==='BOOK');assert.equal(books.length,6);
for(let i=0;i<6;i++){const book=books.find(b=>b.id===0x01000810+i);const data=book.fields.find(f=>f.tag==='DATA').data;assert(data[0]&4);assert.equal(data.readUInt32LE(4),0x01000000+[0x804,0x805,0x80A,0x80B,0x80C,0x80D][i]);}
assert(!/player->AddSpell\(/.test(implementation));
assert(fs.existsSync(path.join(dist,'meshes/NPCMounts/restraintpost_static.nif')));
const report={pluginRecords:22,spellTomes:6,onlyMaster:'Skyrim.esm',noNPCOrRaceOverrides:true,validatedUniqueClips:18,animationAliases:hkx,mutuallyExclusiveRanks:true,gameTested:false};
fs.writeFileSync(path.join(repo,'reports/package-verification.json'),JSON.stringify(report,null,2));console.log(report);
