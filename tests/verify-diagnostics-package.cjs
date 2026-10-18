const fs=require('fs'),path=require('path'),assert=require('assert');
const next=fs.readFileSync(path.join(process.argv[3],'SKSE/Plugins/NPCMounts.ini'),'utf8');
assert.equal((next.match(/^LogLevel=[0-4]\r?$/gm)||[]).length,1);
assert(!/^DebugLog=/m.test(next));
const plugin=fs.readFileSync('src/plugin.cpp','utf8');
assert(plugin.includes('rideSamples < 1200')&&plugin.includes('rideLogTimer = 0.5f;')&&plugin.includes('Diagnostics::Sample(slot)'));
assert(plugin.includes('AddEventSink<RE::TESSpellCastEvent>'));
assert(plugin.includes('config.logLevel>=4'));
console.log('PASS: LogLevel replaces DebugLog; full diagnostics remain bounded and require level 4. Not game QA.');
