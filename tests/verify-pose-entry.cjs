const fs=require('fs'),vm=require('vm'),assert=require('assert');
const prefix=fs.readFileSync('tools/inspect-ride-entrypoints.cjs','utf8').split('const movement=')[0];
const a=vm.runInNewContext(prefix+';({bytes,ids})',{require,console});
const old=Number(a.bytes(a.ids.get(275301)+0x17*8,8).readBigUInt64LE())-0x140000000;
assert.equal(old,0x9f05c0);
assert.equal(a.bytes(old,3).toString('hex'),'498b08'); // child output pointer, not final evaluation.
assert.equal(a.bytes(0x9f1230,15).toString('hex'),'48895c2408488974241048897c2418');
// Installed actor caller: stack bool/float, R9B=false, R8=generatorOutputs[0], RDX=context, RCX=behaviorGraph.
assert.equal(a.bytes(0xaec684,35).toString('hex'),'44886424280f57c0f30f114424204533c94c8b8320020000488d55e0488b8b08020000');
assert.equal(a.bytes(0xaec6a7,1)[0],0xe8);
assert.equal(0xaec6ac+a.bytes(0xaec6a8,4).readInt32LE(),0x9f1230);
// The root generator forwards context/output into its evaluation data and then finishes all modifiers.
assert.equal(a.bytes(0x9f1261,6).toString('hex'),'4d8bf04c8bfa');
assert.equal(a.bytes(0x9f1497,1)[0],0xe8);
assert.equal(0x9f149c+a.bytes(0x9f1498,4).readInt32LE(),0x9f1de0);
console.log('PASS: installed EXE root vs referenced-subgraph entries and native caller ABI verified. Not game QA.');
