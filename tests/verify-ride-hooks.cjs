const fs=require('fs'),assert=require('assert');
const source=fs.readFileSync('src/RideHooks.h','utf8'),plugin=fs.readFileSync('src/plugin.cpp','utf8');
function body(sig){const start=source.indexOf(sig);assert(start>=0);let end=source.indexOf('{',start)+1,d=1;while(d){const c=source[end++];if(c==='{')d++;if(c==='}')d--;}return source.slice(start,end);}
const update=plugin.slice(plugin.indexOf('void NPCUpdate('),plugin.indexOf('using Activate ='));
assert(update.includes('const auto previousPlacement=RideHooks::placedActor;'));
assert(update.indexOf('RideHooks::placedActor=placement;')<update.indexOf('originalNPCUpdate(actor, delta)'));
assert(update.indexOf('RideHooks::placedActor=previousPlacement;')>update.indexOf('originalNPCUpdate(actor, delta)'));
assert(plugin.includes('if(!RideHooks::installed)'));
assert(source.includes('SKSE::AllocTrampoline(32)'));
fs.writeFileSync('build/ride-hooks-regression.cpp',`
#include <cassert>
#include <cstdio>
#include <thread>
namespace RE{struct Actor{int moves=0;};struct ThirdPersonState{bool freeRotationEnabled=false;};}
namespace RideDrive{bool owned=false;}
namespace RideHooks{
thread_local RE::Actor* placedActor=nullptr;
int cameraCalls=0;float lastDelta=0;bool lastSheathed=false;
void NativeMovement(RE::Actor* a,float d){if(a)a->moves++;lastDelta=d;}
void NativeCamera(RE::ThirdPersonState* s,bool b){++cameraCalls;lastSheathed=b;s->freeRotationEnabled=b;}
auto originalMovement=NativeMovement;auto originalCamera=NativeCamera;
${body('inline void MovementDelta(')}
${body('inline void FreeRotation(')}
}
int main(){using namespace RideHooks;RE::Actor player,mount,other;
 MovementDelta(&player,.03f);assert(player.moves==1&&lastDelta==.03f);
 placedActor=&mount;MovementDelta(&mount,.02f);assert(mount.moves==0);
 MovementDelta(&player,.02f);MovementDelta(&other,.02f);assert(player.moves==2&&other.moves==1);
 // Thread-local suppression cannot leak into a separate engine worker.
 std::thread worker([&]{assert(!placedActor);MovementDelta(&mount,.04f);});worker.join();assert(mount.moves==1);
 placedActor=nullptr;MovementDelta(&mount,.05f);assert(mount.moves==2&&lastDelta==.05f);
 RE::ThirdPersonState camera;FreeRotation(&camera,false);assert(cameraCalls==1&&!camera.freeRotationEnabled);
 RideDrive::owned=true;FreeRotation(&camera,false);assert(cameraCalls==1&&camera.freeRotationEnabled);
 RideDrive::owned=false;FreeRotation(&camera,false);assert(cameraCalls==2&&!camera.freeRotationEnabled);
 FreeRotation(&camera,true);assert(cameraCalls==3&&lastSheathed);
 puts("PASS: actual hook bodies isolate the followed actor, preserve player/unrelated movement and delta, thread-local isolation, camera ownership released. Not engine QA.");
}
`);
