// Execute queued activation and real Revert, including a load between press and dispatch.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'src/plugin.cpp'),'utf8');
function take(sig){const start=source.indexOf(sig);assert(start>=0);let end=source.indexOf('{',start)+1,n=1;while(n){const c=source[end++];if(c==='{')n++;if(c==='}')n--;}return source.slice(start,end);}
fs.writeFileSync(path.join(root,'build/queued-actions-regression.cpp'),`
#include <cassert>
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstdio>
#include "../src/RideControl.h"
namespace RE {
struct Actor;struct Ref{Actor* p;Actor* get(){return p;}explicit operator bool()const{return p;}};
struct ActorHandle{Actor* p=nullptr;Ref get()const{return{p};}explicit operator bool()const{return p;}void reset(){p=nullptr;}};
struct Actor{ActorHandle GetHandle(){return{this};}};
struct ActivateHandler{};struct ButtonEvent{bool IsDown(){return true;}};
struct InputEvent{int action=1;ButtonEvent button;int QUserEvent(){return action;}ButtonEvent* AsButtonEvent(){return &button;}};
struct UserEvents{int activate=1;static UserEvents* GetSingleton(){static UserEvents e;return &e;}};
}
namespace SKSE{struct SerializationInterface{};struct Tasks{std::vector<std::function<void()>>queue;void AddTask(std::function<void()> fn){queue.push_back(fn);}void Run(){auto pending=std::move(queue);queue.clear();for(auto& fn:pending)fn();}}tasks;Tasks* GetTaskInterface(){return &tasks;}}
namespace Extras{unsigned epoch=1;unsigned Session(){return epoch;}void Revert(){++epoch;}}
namespace Seat{void Clear(){}}
RE::Actor actor;RE::ActorHandle ridden;int mounts=0;bool converted=true;
std::unordered_map<unsigned,int>targets;std::unordered_set<std::uint64_t>suppressedButtons;
HoldGate dismountHold;bool downActive=false,loadedRideState=false,wasAir=false;float playerSpeedDelta=0,jumpTimer=0,landTimer=0,restoreRetry=0;int rideGait=0;
RE::Actor* Crosshair(){return &actor;}bool Converted(RE::Actor*){return converted;}
bool originalActivate(RE::ActivateHandler*,RE::InputEvent*){return true;}void Mount(RE::Actor* a){++mounts;ridden=a->GetHandle();}
${take('bool ActivateInput(')}
${take('void Revert(')}
int main(){RE::ActivateHandler handler;RE::InputEvent event;
 assert(!ActivateInput(&handler,&event)&&mounts==0&&SKSE::tasks.queue.size()==1);SKSE::tasks.Run();assert(mounts==1&&ridden);ridden.reset();
 ActivateInput(&handler,&event);suppressedButtons.insert(29);dismountHold.Press();targets[1]=1;Revert(nullptr);assert(suppressedButtons.empty()&&targets.empty()&&!dismountHold.pressed);
 SKSE::tasks.Run();assert(mounts==1&&!ridden); // old save's delayed mount cannot run in the new save
 ActivateInput(&handler,&event);ridden=actor.GetHandle();SKSE::tasks.Run();assert(mounts==1);ridden.reset();
 converted=false;assert(ActivateInput(&handler,&event)&&SKSE::tasks.queue.empty());converted=true;
 for(int i=0;i<10000;++i){auto before=mounts;ActivateInput(&handler,&event);if(i%2==0)Revert(nullptr);SKSE::tasks.Run();assert(mounts==before+(i%2));ridden.reset();assert(SKSE::tasks.queue.empty());}
 std::puts("PASS: actual activation defers mount, old-save tasks rejected after Revert, riding race avoided, suppressed keys and hold gate cleared.");
}
`);
