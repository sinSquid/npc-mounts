// Production teleport detector, lifecycle, serialization and landing selection.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),s=fs.readFileSync(path.join(root,'src/Travel.cpp'),'utf8');
function take(sig){let start=s.indexOf(sig);assert(start>=0,sig);let end=s.indexOf('{',start)+1,n=1;while(n){const c=s[end++];if(c==='{')n++;if(c==='}')n--;}return s.slice(start,end);}
fs.writeFileSync(path.join(root,'build/travel-regression.cpp'),`
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <numbers>
#include <vector>
#include "../src/TravelState.h"
namespace RE{
enum class BSEventNotifyControl{kContinue};template<class T>struct BSTEventSource{};template<class T>struct BSTEventSink{virtual BSEventNotifyControl ProcessEvent(const T*,BSTEventSource<T>*)=0;};struct PositionPlayerEvent{enum class EVENT_TYPE{kPre,kFinish};EVENT_TYPE type;};
using FormID=unsigned;struct NiPoint3{float x=0,y=0,z=0;NiPoint3 operator+(NiPoint3 b)const{return{x+b.x,y+b.y,z+b.z};}NiPoint3 operator-(NiPoint3 b)const{return{x-b.x,y-b.y,z-b.z};}NiPoint3 operator*(float v)const{return{x*v,y*v,z*v};}float Length()const{return std::sqrt(x*x+y*y+z*z);}};
struct Cell{unsigned id=1;bool interior=false;unsigned GetFormID(){return id;}bool IsInteriorCell(){return interior;}};struct World{unsigned id=10;unsigned GetFormID(){return id;}};struct bhkWorld{};struct bhkPickData{struct{float hitFraction=.5f;}rayOutput;};
struct Controller{bhkWorld world;bhkWorld* GetHavokWorld(){return &world;}};
struct Actor{unsigned id=1;Cell* cell;World* world;NiPoint3 pos;Controller cc;bool ready=true,loaded=true,converted=true,restoring=false,dead=false,deleted=false,disabled=false,kill=false,mounted=false,bound=false,busy=false,riding=false;float scale=1;
 unsigned GetFormID(){return id;}Cell* GetParentCell(){return cell;}World* GetWorldspace(){return world;}NiPoint3 GetPosition(){return pos;}float GetScale(){return scale;}float GetAngleZ(){return 0;}Controller* GetCharController(){return &cc;}bool IsDead(){return dead;}bool IsDeleted(){return deleted;}bool IsDisabled(){return disabled;}bool IsInKillMove(){return kill;}bool IsOnMount(){return mounted;}void* Get3D(){return loaded?this:nullptr;}};
Actor* selected=nullptr;struct TESForm{template<class T>static T* LookupByID(unsigned id){return selected&&selected->id==id?selected:nullptr;}};
struct LoadingMenu{static constexpr int MENU_NAME=1;};struct UI{bool loading=false,paused=false;static UI* GetSingleton(){static UI ui;return &ui;}bool IsMenuOpen(int){return loading;}bool GameIsPaused(){return paused;}};
}
namespace SKSE{struct SerializationInterface{unsigned tag=0,version=0,value=0;bool resolve=true;void OpenRecord(unsigned t,unsigned v){tag=t;version=v;}void WriteRecordData(unsigned v){value=v;}unsigned ReadRecordData(void* p,unsigned n){std::memcpy(p,&value,n);return n;}bool ResolveFormID(unsigned id,unsigned& out){if(resolve)out=id+100;return resolve;}};}
namespace spdlog{template<class...T>void info(const char*,T...){}}
namespace Mounts{bool Converted(RE::Actor* a){return a->converted;}bool Restoring(RE::Actor* a){return a->restoring;}bool Riding(RE::Actor* a){return a->riding;}bool ActorReady(RE::Actor* a){return a->ready;}bool walls=false,floors=true;int rays=0;
 bool Ray(RE::bhkWorld*,const RE::NiPoint3& a,const RE::NiPoint3& b,RE::bhkPickData& hit){++rays;if(a.z==b.z)return walls;hit.rayOutput.hitFraction=64.f/224.f;return floors;}}
namespace Extras{bool Bound(RE::Actor* a){return a->bound;}bool Busy(RE::Actor* a){return a->busy;}}
namespace Travel{
RE::FormID last=0;Tracker tracker;float probe=0;int moves=0;std::atomic<bool> armed=false,arrived=false;
${take('struct Arrival final')};Arrival arrival;
void Relocate(RE::Actor* a,RE::Actor* p,const RE::NiPoint3& position){++moves;a->pos=position;a->cell=p->cell;}
${['void Remember(','void Forget(','void Revert(','void Save(','bool Load(','bool Landing(','void Tick('].map(take).join('\n')}
}
int main(){using namespace Travel;
 RE::PositionPlayerEvent signal{RE::PositionPlayerEvent::EVENT_TYPE::kFinish};arrival.ProcessEvent(&signal,nullptr);assert(!arrived);armed=true;signal.type=RE::PositionPlayerEvent::EVENT_TYPE::kPre;arrival.ProcessEvent(&signal,nullptr);assert(!arrived);signal.type=RE::PositionPlayerEvent::EVENT_TYPE::kFinish;arrival.ProcessEvent(&signal,nullptr);assert(arrived);Revert();assert(!arrived&&!armed);
 // Ordinary walking, exterior cell boundaries, and save-load baseline never warp.
 Tracker t;Location l{1,10,false,0,0,0};assert(!t.Observe(l,.016f));for(int i=0;i<20000;i++){l.x+=8;l.cell=1+i/500;assert(!t.Observe(l,.016f));}t.Gap();assert(!t.Observe(l,.016f));
 for(float fps:{30.f,60.f,144.f}){t={};l={1,10,false,0,0,0};t.Observe(l,1/fps);l.cell=2;l.interior=true;int frames=0;while(!t.Observe(l,1/fps)){assert(++frames<fps*2);}assert(frames/fps>=.7f);t.Done();assert(!t.Observe(l,1/fps));}
 t={};l={1,10,false,0,0,0};t.Observe(l,.016f);l.x=2000;t.Observe(l,.016f);assert(t.pending);l.world=11;t.Observe(l,.016f);assert(t.settle<.1f);for(int i=0;i<110;i++)t.Observe(l,.1f);assert(!t.pending);
 t={};l={1,10,false,0,0,0};t.Observe(l,.016f);t.Gap();l.x=200;t.Observe(l,.016f);assert(t.pending);t={};assert(!t.Observe(l,.016f));
 RE::Cell origin{1,false},dest{2,true};RE::World world;RE::Actor p{1,&origin,&world},a{2,&origin,&world};RE::selected=&a;
 auto run=[&](int n){for(int i=0;i<n;i++)Tick(&p,.02f);};
 auto travel=[&](){Revert();p.cell=&origin;p.pos={0,0,0};a.cell=&origin;a.pos={-500,0,0};Remember(&a);run(1);p.cell=&dest;p.pos={5000,0,0};};
 travel();run(20);assert(moves==0);run(60);assert(moves==1&&a.cell==&dest&&(a.pos-p.pos).Length()>60);run(1000);assert(moves==1); // one native call only
 for(int gate=0;gate<5;gate++){travel();auto before=moves;a.bound=gate==0;a.busy=gate==1;a.riding=gate==2;a.disabled=gate==3;a.restoring=gate==4;run(100);assert(moves==before);a.bound=a.busy=a.riding=a.disabled=a.restoring=false;}
 travel();Forget(a.id);run(100);assert(last==0);travel();a.dead=true;run(100);assert(last==0);a.dead=false;
 Revert();p.cell=&origin;p.pos={0,0,0};a.cell=&origin;a.pos={-900,0,0};Remember(&a);run(1);auto shortBefore=moves;p.pos={64,0,0};arrived=true;run(100);assert(moves==shortBefore+1); // real finish signal covers short same-cell teleport
 travel();auto before=moves;RE::UI::GetSingleton()->loading=true;run(100);assert(moves==before);RE::UI::GetSingleton()->loading=false;p.ready=false;run(100);assert(moves==before);p.ready=true;run(100);assert(moves==before+1);
 travel();before=moves;a.loaded=true;a.ready=false;run(60);assert(moves==before);a.loaded=false;run(30);assert(moves==before+1);a.loaded=a.ready=true;
 travel();before=moves;Mounts::floors=false;run(600);assert(moves==before&&!tracker.pending);Mounts::floors=true;run(100);assert(moves==before); // no delayed infinite retry
 RE::NiPoint3 landing;Mounts::walls=true;assert(!Landing(&p,&a,landing));Mounts::walls=false;assert(Landing(&p,&a,landing)&&std::abs(landing.z-p.pos.z-5)<.001f);
 Remember(&a);SKSE::SerializationInterface save;Save(&save);assert(save.tag=='TRVL'&&save.version==1&&save.value==a.id);Revert();assert(last==0&&!tracker.initialized);assert(Load(&save,'TRVL',1,4)&&last==a.id+100&&!tracker.initialized);assert(!Load(&save,'NMNT',5,4));save.resolve=false;assert(Load(&save,'TRVL',1,4)&&last==0);assert(Load(&save,'TRVL',2,4)&&last==0);assert(Load(&save,'TRVL',1,8)&&last==0);
 std::puts("PASS: actual travel tracker/Tick/landing/save-load: normal exterior walking, doors/world/instant relocation, loading gap, settling at 30/60/144 FPS, one shot, fixed/leashed/restoring/dead targets, queued source, no floor timeout, ID resolution and clean new-save baseline.");}
`);
