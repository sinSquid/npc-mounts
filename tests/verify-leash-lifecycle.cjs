// Execute the production asynchronous lifecycle with queued VM test doubles.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'src/Extras.cpp'),'utf8');
function take(sig){const start=source.indexOf(sig);assert(start>=0);let end=source.indexOf('{',start)+1,depth=1;while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}return source.slice(start,end);}
const plugin=fs.readFileSync(path.join(root,'src/plugin.cpp'),'utf8');
assert(plugin.indexOf('if (Extras::Bound(actor) || Extras::Busy(actor))',plugin.indexOf('bool Convert('))<plugin.indexOf('targets.emplace',plugin.indexOf('bool Convert(')));
assert(source.includes('if(session.load()!=epoch)return;'));
fs.writeFileSync(path.join(root,'build/leash-lifecycle-regression.cpp'),`
#include <cassert>
#include <cstdio>
#include <string>
#include <functional>
#include <unordered_map>
#include <deque>
#include <cstdint>
#include <atomic>
#include <algorithm>
namespace RE {using FormID=unsigned;struct Actor;
struct Ref {Actor* p;Actor* get(){return p;}explicit operator bool()const{return p;}};
struct Handle {Actor* p;Ref get()const{return {p};}};
struct Actor {unsigned id=1;bool converted=true,bound=false,riding=false;unsigned GetFormID(){return id;}Handle GetHandle(){return {this};}template<class T>T* As(){return static_cast<T*>(this);}};
struct PlayerCharacter:Actor {static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;}};
}
namespace Mounts {bool Converted(RE::Actor* a){return a&&a->converted;}bool Riding(RE::Actor* a){return a->riding;}}
${take('enum class Phase ')};
${take('struct Tether ')};
std::unordered_map<RE::FormID,Tether> tethers;std::uint64_t serial=0;bool leashAvailable=true,dispatch=true;
std::atomic_uint64_t leashEventsSeen{0},session{1};
struct Pending {std::string name;std::function<void(bool)> callback;};std::deque<Pending> queue;
void Notify(const char*){}bool Eligible(RE::Actor* a){return a!=nullptr;}bool Bound(RE::Actor* a){return a&&a->bound;}
${take('bool Busy(')}
${take('bool Leashed(')}
template<class... Args>bool Call(const char* name,std::function<void(bool)> fn,Args...){if(!dispatch)return false;queue.push_back({name,std::move(fn)});return true;}
void Reply(const char* name,bool value){assert(!queue.empty()&&queue.front().name==name);auto fn=queue.front().callback;queue.pop_front();fn(value);}
${take('bool Current(')}
${take('void FailTether(')}
${take('void StopTether(')}
${take('void StartTether(')}
${take('void CheckTether(')}
${take('void AdvanceTethers(')}
${take('void Unleashed(')}
int main(){RE::Actor actor;
 leashAvailable=false;StartTether(&actor);assert(tethers.empty()&&queue.empty());leashAvailable=true;
 actor.bound=true;StartTether(&actor);assert(tethers.empty());actor.bound=false;
 actor.converted=false;StartTether(&actor);assert(tethers.empty());actor.converted=true;
 actor.riding=true;StartTether(&actor);assert(tethers.empty());actor.riding=false;
 StartTether(&actor);Reply("IsLeashed",true);assert(tethers.empty()&&queue.empty());
 StartTether(&actor);StopTether(&actor);Reply("IsLeashed",false);assert(tethers.empty()&&queue.empty());
 StartTether(&actor);Reply("IsLeashed",false);StopTether(&actor);assert(Busy(&actor));Reply("ApplyStandaloneLeash",true);assert(!Leashed(&actor));Reply("DisconnectLeash",true);assert(!Busy(&actor));
 StartTether(&actor);Reply("IsLeashed",false);Reply("ApplyStandaloneLeash",false);assert(!Busy(&actor));
 StartTether(&actor);Reply("IsLeashed",false);Reply("ApplyStandaloneLeash",true);Reply("SetRagdollOverride",false);assert(Leashed(&actor));
 dispatch=false;StopTether(&actor);assert(Busy(&actor)&&tethers.at(1).phase==Phase::Active&&!Leashed(&actor));dispatch=true;
 AdvanceTethers(1);StopTether(&actor);Reply("DisconnectLeash",false);assert(Busy(&actor)&&tethers.at(1).phase==Phase::Reconciling&&!tethers.at(1).pending);
 AdvanceTethers(1);CheckTether(&actor);Reply("GetLeashHolder",false);assert(!Busy(&actor));
 dispatch=false;StartTether(&actor);assert(!Busy(&actor));dispatch=true;
 StartTether(&actor);auto old=queue.front().callback;queue.clear();tethers.clear();++serial;
 StartTether(&actor);const auto token=tethers.at(1).token;old(true);assert(tethers.at(1).token==token);Reply("IsLeashed",false);Reply("ApplyStandaloneLeash",true);Reply("SetRagdollOverride",false);
 StopTether(&actor);Reply("DisconnectLeash",true);assert(tethers.empty());
 // Preflight timeout cannot create anything; its late callback must be inert.
 StartTether(&actor);AdvanceTethers(10);assert(!Busy(&actor));Reply("IsLeashed",false);assert(queue.empty());
 // Accepted creation is never dispatched twice. B/F cancellation survives a late result.
 StartTether(&actor);Reply("IsLeashed",false);AdvanceTethers(10);assert(tethers.at(1).phase==Phase::Failed&&tethers.at(1).pending);
 for(int i=0;i<1000;i++)StopTether(&actor);assert(queue.size()==1);
 Reply("ApplyStandaloneLeash",true);Reply("DisconnectLeash",true);assert(!Busy(&actor));
 // Three dispatch failures, not one per frame forever. F may explicitly request reconciliation.
 tethers[1]={Phase::Active,true,++serial};dispatch=false;
 for(int i=0;i<10000;i++){AdvanceTethers(.02f);StopTether(&actor);}assert(tethers.at(1).phase==Phase::Failed&&tethers.at(1).failures==3);dispatch=true;
 tethers.clear();tethers[1]={Phase::Reconciling,false,++serial};CheckTether(&actor);
 for(int i=0;i<1000;i++)CheckTether(&actor);assert(queue.size()==1);Reply("GetLeashHolder",false);assert(!Busy(&actor));
 tethers[1]={Phase::Reconciling,false,++serial};CheckTether(&actor);Reply("GetLeashHolder",true);assert(!Leashed(&actor));StopTether(&actor);
 AdvanceTethers(10);assert(tethers.at(1).phase==Phase::Failed);for(int i=0;i<1000;i++)StopTether(&actor);assert(queue.size()==1);
 Reply("DisconnectLeash",true);assert(!Busy(&actor));
 // False disconnect with ownership still present retries at most three times.
 tethers[1]={Phase::Active,false,++serial};
 for(unsigned i=1;i<=3;++i){StopTether(&actor);Reply("DisconnectLeash",false);assert(Busy(&actor)&&tethers.at(1).failures==i);
  if(i<3){AdvanceTethers(1);CheckTether(&actor);Reply("GetLeashHolder",true);assert(tethers.at(1).failures==i);}}
 assert(tethers.at(1).phase==Phase::Failed&&!tethers.at(1).pending);for(int i=0;i<1000;++i){AdvanceTethers(.02f);StopTether(&actor);}assert(queue.empty());tethers.clear();
 // A missing query callback does not accumulate more queries or allow locomotion.
 tethers[1]={Phase::Active,true,++serial};CheckTether(&actor);AdvanceTethers(10);assert(!Leashed(&actor)&&tethers.at(1).phase==Phase::Failed);
 for(int i=0;i<1000;i++)CheckTether(&actor);assert(queue.size()==1);Reply("GetLeashHolder",true);assert(tethers.at(1).phase==Phase::Failed);
 tethers.at(1).phase=Phase::Reconciling;CheckTether(&actor);Reply("GetLeashHolder",true);StopTether(&actor);Reply("DisconnectLeash",true);
 tethers[1]={Phase::Active,true,++serial};tethers.at(1).born=5;
 Unleashed(1,0,6);assert(Busy(&actor)); // previous save
 Unleashed(1,1,5);assert(Busy(&actor)); // previous rope in this save
 Unleashed(1,1,6);assert(!Busy(&actor)); // current rope's event
 // Repeated cycles must not leave pending callbacks or block the next E.
 for(int i=0;i<1000;++i){StartTether(&actor);if(i%4==0)StopTether(&actor);Reply("IsLeashed",false);
  if(i%4==0){assert(!Busy(&actor)&&queue.empty());continue;}
  if(i%4==1)StopTether(&actor);Reply("ApplyStandaloneLeash",true);
  if(i%4!=1){Reply("SetRagdollOverride",false);StopTether(&actor);}
  Reply("DisconnectLeash",i%4!=2);
  if(i%4==2){assert(Busy(&actor));AdvanceTethers(1);CheckTether(&actor);Reply("GetLeashHolder",false);}
  assert(!Busy(&actor)&&queue.empty());}
 std::puts("PASS: actual E/F lifecycle and 1000 repeated cancellation/reconciliation cycles; invalid targets, timeout/retry bounds and stale callback isolation. Not VM/engine QA.");
}
`);
