// Execute the production NPCUpdate scope and anchor gate together. Engine calls are doubles.
const fs=require('fs'),assert=require('assert');
function take(file,sig){const s=fs.readFileSync(file,'utf8'),start=s.indexOf(sig);assert(start>=0,sig);let end=s.indexOf('{',start)+1,n=1;while(n){const c=s[end++];if(c==='{')n++;if(c==='}')n--;}return s.slice(start,end);}
const extra=s=>take('src/Extras.cpp',s);
fs.writeFileSync('build/anchor-update-regression.cpp',`
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <functional>
#include <vector>
namespace RE {
using FormID=unsigned;
struct NiPoint3{float x=0,y=0,z=0;NiPoint3 operator-(NiPoint3 p)const{return{x-p.x,y-p.y,z-p.z};}float Length()const{return std::sqrt(x*x+y*y+z*z);}};
struct hkVector4{};
struct TESObjectCELL{unsigned id=1;bool attached=true;struct{void* worldSpace=nullptr;}data;auto& GetRuntimeData(){return data;}unsigned GetFormID(){return id;}bool IsAttached(){return attached;}};
inline TESObjectCELL cell;
struct TESForm{template<class T>static T* LookupByID(unsigned id){return id==1?&cell:nullptr;}};
struct Actor{unsigned id=10;bool dead=false,loaded=true;NiPoint3 pos;TESObjectCELL* parent=&cell;
 struct Controller{float speed=300;void SetLinearVelocityImpl(hkVector4){speed=0;}}cc;
 unsigned GetFormID(){return id;}bool IsDead(){return dead;}bool IsInKillMove(){return false;}auto* GetParentCell(){return parent;}
 void AddToFaction(void*,int){}void NotifyAnimationGraph(const char*){}
 struct Ref{Actor* p;Actor* get(){return p;}Actor* operator->(){return p;}explicit operator bool()const{return p;}};
 struct Handle{Actor* p;Ref get()const{return {p};}};Handle GetHandle(){return {this};}
 void* Get3D(){return loaded?this:nullptr;}auto* GetCharController(){return &cc;}auto GetPosition(){return pos;}
};
struct PlayerCharacter{static Actor* GetSingleton(){static Actor p;return &p;}};
struct UI{bool paused=false;static UI* GetSingleton(){static UI ui;return &ui;}bool GameIsPaused(){return paused;}};
}
namespace Extras {
unsigned epoch=1;unsigned Session(){return epoch;}
${extra('struct Binding {')};
Binding binding;bool bound=true;
bool Find(unsigned id,Binding& b){b=binding;return bound&&id==10;}
${extra('bool Ready(')}
${extra('bool SameSpace(')}
${extra('bool Displaced(')}
${extra('void Freeze(')}
${extra('bool BlockMovement(')}
bool Leashed(RE::Actor*){return false;}void AfterActorUpdate(RE::Actor*){}
}
namespace RideHooks{
thread_local RE::Actor* placedActor=nullptr;
int nativeMoves=0;
void originalMovement(RE::Actor* a,float dt){++nativeMoves;a->pos.x+=100*dt;}
${take('src/RideHooks.h','inline void MovementDelta(')}
}
struct Ptr{RE::Actor* get(){return nullptr;}};struct Handle{Ptr get(){return {};}}ridden;
bool Riding(RE::Actor*){return false;}void AuditMotionStage(RE::Actor*,const char*){}void AuditLocation(RE::Actor*){}void HoldPose(RE::Actor*){}
bool RideReady(RE::Actor*,RE::Actor*){return true;}void SyncMount(RE::Actor*,RE::Actor*){}
struct State{bool posePending=false,restoreRequested=false;};std::unordered_map<unsigned,State>targets;void* mountFaction=nullptr;
bool Converted(RE::Actor* a){return targets.contains(a->id);}bool ActorReady(RE::Actor* a){return a->loaded;}
namespace Crawl{int resumes=0;bool released=false;bool Resume(RE::Actor*){if(released)return false;released=true;++resumes;return true;}}
namespace SKSE{struct Tasks{std::vector<std::function<void()>> queue;void AddTask(std::function<void()> f){queue.push_back(f);}void Run(){auto work=queue;queue.clear();for(auto& f:work)f();}}tasks;Tasks* GetTaskInterface(){return &tasks;}}
int frames=0,npcUpdates=0;float lastNPCDrift=0;
void originalNPCUpdate(RE::Actor* a,float dt){++frames;RideHooks::MovementDelta(a,dt);}
${take('src/plugin.cpp','void NPCUpdate(')}
int main(){
 RE::Actor actor;Extras::binding.post=3;Extras::binding.cell=1;
 for(int i=0;i<1000;++i)NPCUpdate(&actor,.016f);
 assert(actor.pos.x==0&&actor.cc.speed==0&&frames==1000&&RideHooks::nativeMoves==0&&!RideHooks::placedActor);
 RE::Actor other;other.id=11;NPCUpdate(&other,.016f);assert(other.pos.x>0&&RideHooks::nativeMoves==1);
 Extras::binding.releaseRequested=true;NPCUpdate(&actor,.016f);assert(actor.pos.x>0&&RideHooks::nativeMoves==2);Extras::binding.releaseRequested=false;
 actor.pos={1000,200,300};NPCUpdate(&actor,.016f);assert(actor.pos.x>=1000&&actor.pos.y==200&&RideHooks::nativeMoves==3); // No teleport rollback.
 actor.pos={};RE::Actor outer;RideHooks::placedActor=&outer;NPCUpdate(&actor,.016f);assert(RideHooks::placedActor==&outer);RideHooks::placedActor=nullptr;
 Extras::bound=false;NPCUpdate(&actor,.016f);assert(actor.pos.x>0);
 targets[actor.id].posePending=true;NPCUpdate(&actor,.016f);assert(targets[actor.id].posePending&&SKSE::tasks.queue.size()==1);SKSE::tasks.Run();assert(!targets[actor.id].posePending&&Crawl::resumes==1);
 NPCUpdate(&actor,.016f);assert(Crawl::resumes==1);
 targets[actor.id]={true,true};Crawl::released=false;NPCUpdate(&actor,.016f);SKSE::tasks.Run();assert(targets[actor.id].posePending); // B/pending restore does not restart conversion.
 puts("PASS: production NPCUpdate + anchor gate + movement hook: 1000 fixed frames still animate, no autonomous translation; release/other actors/forced relocation pass, nested scope restored. Engine doubles, not game QA.");
}
`);
