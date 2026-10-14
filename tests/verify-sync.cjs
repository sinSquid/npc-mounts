// Model the critical engine contract: a reference and a physics proxy are distinct.
// Execute the actual SyncMount/RestoreController bodies, not a rewritten algorithm.
const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/plugin.cpp'),'utf8');
function extract(name){
 const start=source.indexOf('void '+name+'(');assert(start>=0);const open=source.indexOf('{',start);
 let end=open+1,depth=1;while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}
 return source.slice(start,end);
}
assert(source.includes('npc.write_vfunc(0xAD, NPCUpdate)'));
assert(source.includes('originalNPCUpdate(actor, delta)'));
assert(!source.includes('UpdateDownwardPass'));
const code=`
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include "../src/Config.h"
Settings::Data config;
namespace RE {
struct NiPoint3 {float x{},y{},z{};bool operator==(const NiPoint3&)const=default;};
struct hkVector4 {};
enum class CHARACTER_FLAGS : unsigned {kNoSim=1u<<17,kNoCharacterCollisions=1u<<27};
struct Flags {
 unsigned value{};
 template<class T>void reset(T bits){value&=~static_cast<unsigned>(bits);}
 template<class T>void set(T bits){value|=static_cast<unsigned>(bits);}
};
struct Controller {Flags flags;NiPoint3 position;float fallTime{};void SetLinearVelocityImpl(hkVector4){};};
struct Actor {
 NiPoint3 position;Controller controller;float angle{};unsigned writes{};
 auto* GetCharController(){return &controller;}
 auto GetPosition(){return position;}auto GetAngleZ(){return angle;}
 void SetAngle(NiPoint3 value){angle=value.z;}
 void SetPosition(NiPoint3 value,bool update){position=value;++writes;if(update)controller.position=value;}
 void EngineUpdate(){position=controller.position;}
};
}
${extract('SyncMount')}
${extract('RestoreController')}
int main(){
 RE::Actor player,mount;mount.controller.flags.value=(1u<<17)|(1u<<21);
 const auto owned=static_cast<RE::CHARACTER_FLAGS>((1u<<17)|(1u<<27));
 const unsigned saved=mount.controller.flags.value&static_cast<unsigned>(owned);
 // Reproduce the old bug: false moves the reference, then physics puts it back.
 mount.SetPosition({10,0,0},false);mount.EngineUpdate();assert(mount.position.x==0);
 for(int i=0;i<2500;++i){
   player.position={float(i)*3,float(i)*2,0};player.angle=float(i)*0.01f;
   SyncMount(&mount,&player);mount.EngineUpdate();
   assert(mount.position==player.position&&mount.controller.position==player.position);
   assert(mount.angle==player.angle);
   assert(!(mount.controller.flags.value&(1u<<17)));
   assert(mount.controller.flags.value&(1u<<27));assert(mount.controller.flags.value&(1u<<21));
 }
 auto writes=mount.writes;for(int i=0;i<100;++i)SyncMount(&mount,&player);assert(mount.writes==writes);
 // Mount entry must repair an old stale proxy even if the reference already matches.
 mount.controller.position={-1000,0,0};SyncMount(&mount,&player,true);mount.EngineUpdate();assert(mount.position==player.position);
 RestoreController(&mount,owned,saved);
 assert((mount.controller.flags.value&static_cast<unsigned>(owned))==saved);
 assert(mount.controller.flags.value&(1u<<21));
 std::puts("PASS: reproduces reference-only snap-back; actual SyncMount holds reference+proxy across 2500 engine updates; unchanged-position writes skipped; cold proxy repair; original controller flags restored. Model test, not game QA.");
}
`;
fs.writeFileSync(path.join(repo,'build/sync-regression.cpp'),code);
console.log('Generated actual SyncMount/RestoreController regression harness.');
