// Evidence comes from the installed executable, not our hook declaration.
const fs=require('fs'),assert=require('assert'),crypto=require('crypto');
const exe='E:/iniRePather-Skyrim/Mod Organizer 2/Stock Game/SkyrimSE.exe',b=fs.readFileSync(exe);
const pe=b.readUInt32LE(0x3c),sections=pe+24+b.readUInt16LE(pe+20),count=b.readUInt16LE(pe+6);
function bytes(rva,n){for(let i=0;i<count;i++){const p=sections+i*40,base=b.readUInt32LE(p+12),size=b.readUInt32LE(p+16);if(rva>=base&&rva+n<=base+size){const o=b.readUInt32LE(p+20)+rva-base;return b.subarray(o,o+n)}}throw Error('RVA not in file');}
const callee='40534883ec20498bc0488bda458b412c4883c1f08b502ce8e40200008803488bc34883c4205bc3';
assert.equal(bytes(0xdd6780,callee.length/2).toString('hex'),callee);
assert.equal(bytes(0xb27573,5).toString('hex'),'488d542440'); // RDX = stack result address
assert.equal(bytes(0xb2758f,9).toString('hex'),'488b06ff5008803800'); // virtual slot1, dereference RAX
console.log('PASS: installed SkyrimSE raw caller/callee bytes verify result-pointer ABI; SHA256 '+crypto.createHash('sha256').update(b).digest('hex'));
