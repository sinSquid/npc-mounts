// Exercise the actual loaded-event handler and queued callback with engine doubles.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'src/plugin.cpp'),'utf8');
const start=source.indexOf('    RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent*');assert(start>=0);
let end=source.indexOf('{',start)+1,n=1;while(n){const c=source[end++];if(c==='{')n++;if(c==='}')n--;}
const resetStart=source.indexOf('void ResetPose(');let resetEnd=source.indexOf('{',resetStart)+1,depth=1;while(depth){const c=source[resetEnd++];if(c==='{')depth++;if(c==='}')depth--;}
const retryStart=source.indexOf('            if(state.animation<0)ResetPose('),retryEnd=source.indexOf('\n',retryStart);assert(retryStart>=0);
fs.writeFileSync(path.join(root,'build/actor-load-regression.cpp'),`
#include <cassert>
#include <functional>
#include <vector>
#include <unordered_map>
#include <cstdio>
namespace RE {
enum class BSEventNotifyControl{kContinue};template<class T>struct BSTEventSource{};
struct TESObjectLoadedEvent{unsigned formID;bool loaded;};struct Cell{bool attached=true;bool IsAttached(){return attached;}};
struct Actor{Cell cell;bool loaded=true,dead=false;int mutations=0;void* Get3D(){return loaded?this:nullptr;}Cell* GetParentCell(){return &cell;}bool IsDead(){return dead;}
 void AddToFaction(int,int){++mutations;}void NotifyAnimationGraph(const char*){++mutations;}};
inline Actor actor;struct TESForm{template<class T>static T* LookupByID(unsigned id){return id==1?&actor:nullptr;}};
}
namespace Extras{unsigned epoch=1,loads=0;unsigned Session(){return epoch;}void Loaded(RE::Actor*){++loads;}}
namespace Crawl{void Bind(RE::Actor*){}}
namespace SKSE {struct Tasks{std::vector<std::function<void()>>queue;void AddTask(std::function<void()> fn){queue.push_back(fn);}void Run(){auto pending=std::move(queue);queue.clear();for(auto& fn:pending)fn();}};Tasks tasks;Tasks* GetTaskInterface(){return &tasks;}}
struct State{bool restoreRequested=false;int animation=3;};std::unordered_map<unsigned,State>targets{{1,{}}};int mountFaction=0,restores=0;
void Restore(RE::Actor*){++restores;targets.erase(1);}void HoldPose(RE::Actor* a){++a->mutations;}void StopMotion(RE::Actor* a){++a->mutations;}
${source.slice(resetStart,resetEnd)}
void RetryPose(){auto* actor=&RE::actor;const auto& state=targets.at(1);const unsigned id=1;
${source.slice(retryStart,retryEnd)}
}
struct Base{virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent*,RE::BSTEventSource<RE::TESObjectLoadedEvent>*)=0;};
struct Handler:Base{${source.slice(start,end)}};
int main(){Handler h;RE::TESObjectLoadedEvent event{1,true};
 h.ProcessEvent(&event,nullptr);assert(RE::actor.mutations==0&&Extras::loads==0&&SKSE::tasks.queue.size()==1);SKSE::tasks.Run();assert(RE::actor.mutations>0&&Extras::loads==1&&targets.at(1).animation==0);
 auto before=RE::actor.mutations;h.ProcessEvent(&event,nullptr);++Extras::epoch;SKSE::tasks.Run();assert(RE::actor.mutations==before&&Extras::loads==1);
 RE::actor.cell.attached=false;h.ProcessEvent(&event,nullptr);SKSE::tasks.Run();assert(RE::actor.mutations==before&&Extras::loads==1);RE::actor.cell.attached=true;
 RE::actor.loaded=false;h.ProcessEvent(&event,nullptr);SKSE::tasks.Run();assert(RE::actor.mutations==before);RE::actor.loaded=true;
 targets.at(1).animation=-1;RetryPose();assert(targets.at(1).animation==0&&RE::actor.mutations>before);before=RE::actor.mutations;RetryPose();assert(RE::actor.mutations==before);
 targets.at(1).restoreRequested=true;h.ProcessEvent(&event,nullptr);SKSE::tasks.Run();assert(restores==1&&RE::actor.mutations==before);
 event.formID=99;h.ProcessEvent(&event,nullptr);assert(SKSE::tasks.queue.empty());event.formID=1;event.loaded=false;h.ProcessEvent(&event,nullptr);assert(SKSE::tasks.queue.empty());
 std::puts("PASS: loaded event defers mutations; detached/missing 3D and stale-session tasks skipped; pending B restored; non-actors/unload events enqueue nothing.");
}
`);
