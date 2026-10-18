// Installed 1.5.97 instruction evidence for the controller-only jump request.
const fs=require('fs'),vm=require('vm');
const prefix=fs.readFileSync(__dirname+'/inspect-ride-entrypoints.cjs','utf8').split('const movement=')[0];
vm.runInNewContext(prefix+`
const table=ids.get(263065);
const process=Number(bytes(table+4*8,8).readBigUInt64LE())-0x140000000;
assert.equal(process,0x7090a0);
assert.equal(bytes(0x7091b4,1)[0],0xe9);
assert.equal(0x7091b9+bytes(0x7091b5,4).readInt32LE(),0x5d1f80); // ProcessButton -> actor action.
assert.equal(bytes(0xdbeee8,10).toString('hex'),'c7811c02000001000000'); // controller wantState=1.
assert.equal(bytes(0xdbeef2,9).toString('hex'),'f30f11893c020000c3'); // controller jumpHeight, return.
assert.equal(bytes(0x5d2105,8).toString('hex'),'f30f103513028101'); // live base height at 1DE2320.
assert.equal(bytes(0x5d2110,1)[0],0xe8);
assert.equal(0x5d2115+bytes(0x5d2111,4).readInt32LE(),0x28cc60); // TESObjectREFR::GetScale.
assert.equal(bytes(0xdbeee0,8).toString('hex'),'f30f590d64177800'); // world->Havok scale.
assert.equal(bytes(0x1de2320,4).readFloatLE(),64);
console.log('PASS: installed JumpHandler -> actor jump -> controller: state 1, height at 23C, live base height*GetScale*HavokScale. Not game QA.');
`,{require,console});
