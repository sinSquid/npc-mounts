// Read-only inspection of the installed 1.5.97 image and Address Library.
const fs=require('fs'),vm=require('vm'),cp=require('child_process');
const prefix=fs.readFileSync(__dirname+'/inspect-ride-entrypoints.cjs','utf8').split('const movement=')[0];
const api=vm.runInNewContext(prefix+';({ids,bytes,exe,sections,n})',{require,console});
const python='C:/Users/admin/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';
const script="import sys;sys.path.insert(0,'E:/code-lib/me/npc-mounts/build/inspection-deps');from capstone import *;m=Cs(CS_ARCH_X86,CS_MODE_64);[print(hex(i.address),i.mnemonic,i.op_str) for i in m.disasm(bytes.fromhex(sys.argv[2]),int(sys.argv[1],16))]";
function dump(rva,size){console.log(cp.execFileSync(python,['-c',script,rva.toString(16),api.bytes(rva,size).toString('hex')],{encoding:'utf8'}));}
if(process.argv[2]==='function'){
 const target=parseInt(process.argv[3],16),b=api.exe;
 for(let s=0;s<api.n;s++){const p=api.sections+s*40;if(b.toString('ascii',p,p+8).replace(/\0/g,'')!=='.pdata')continue;
  const size=b.readUInt32LE(p+16),raw=b.readUInt32LE(p+20);
  for(let i=0;i<size-12;i+=12){const start=b.readUInt32LE(raw+i),end=b.readUInt32LE(raw+i+4);if(start<=target&&target<end){console.log('function',start.toString(16),end.toString(16));dump(start,end-start);}}
 }
}else if(process.argv[2]==='calls'){
 const target=parseInt(process.argv[3],16),b=api.exe;
 for(let s=0;s<api.n;s++){const p=api.sections+s*40;if(!(b.readUInt32LE(p+36)&0x20000000))continue;
  const base=b.readUInt32LE(p+12),size=b.readUInt32LE(p+16),raw=b.readUInt32LE(p+20);
  for(let i=0;i<size-5;i++)if(b[raw+i]===0xe8&&base+i+5+b.readInt32LE(raw+i+1)===target)console.log((base+i).toString(16));
 }
}else if(process.argv[2])dump(parseInt(process.argv[2],16),Number(process.argv[3]||512));
else{const table=api.ids.get(275301),rva=Number(api.bytes(table+0x17*8,8).readBigUInt64LE())-0x140000000;console.log('hkbBehaviorGraph virtual Generate',rva.toString(16));dump(rva,400);}
