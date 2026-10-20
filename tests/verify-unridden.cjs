// Run the actual Tick branch, including direct-movement and LF fallback routing.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'src/plugin.cpp'),'utf8');
const start=source.indexOf('            int observedAnimation=0;'),end=source.indexOf('            if(!Riding(actor))Crawl::Update',start);
assert(start>0&&end>start);
fs.writeFileSync(path.join(root,'build/unridden-regression.cpp'),`
#include <cassert>
#include <cstdio>
#include "../src/Motion.h"
namespace RE{
struct NiPoint3{float x=0,y=0,z=0;};
struct hkVector4{struct{float m128_f32[4]{};}quad;};
struct bhkWorld{static float GetWorldScaleInverse(){return 1.f;}};
struct Cell{unsigned id=1;unsigned GetFormID(){return id;}};
struct Controller{float speed=0;void GetLinearVelocityImpl(hkVector4& v){v.quad.m128_f32[0]=speed;}};
struct Actor{NiPoint3 position;Cell cell;Controller cc;bool ridden=false,leashed=false,bound=false;
 NiPoint3 GetPosition(){return position;}Cell* GetParentCell(){return &cell;}Controller* GetCharController(){return &cc;}};
}
struct State{int animation=0;bool restoreRequested=false,leashMoving=false;Motion::ObservedMovement unriddenMotion;};
struct{float runThreshold=230,sprintThreshold=540;}config;
bool Riding(RE::Actor* a){return a->ridden;}
namespace Extras{bool Leashed(RE::Actor* a){return a->leashed;}bool Bound(RE::Actor* a){return a->bound;}}
void SetMotion(RE::Actor*,State& s,int animation){s.animation=animation;}
void StopMotion(RE::Actor*){}
void TickPart(RE::Actor* actor,State& state,float delta){
${source.slice(start,end)}
}
int main(){
 for(float fps:{30.f,60.f,144.f})for(bool leash:{false,true}){
  RE::Actor a;State s;a.leashed=leash;const float dt=1/fps;
  TickPart(&a,s,dt);
  for(int i=0;i<int(fps);++i){a.position.x=(i%2)*.1f;TickPart(&a,s,dt);}assert(s.animation==0);
  for(auto [speed,gait]:{std::pair{100.f,1},std::pair{300.f,2},std::pair{600.f,3}}){
   for(int i=0;i<int(fps);++i){a.position.x+=speed*dt;TickPart(&a,s,dt);}
   assert(s.animation==gait&&a.cc.speed==0); // No controller velocity, including on water.
  }
  for(int i=0;i<int(fps);++i){a.position.z+=2;TickPart(&a,s,dt);}assert(s.animation==0); // Vertical-only bob is not walking.
  a.position.x+=5000;TickPart(&a,s,dt);assert(s.animation==0);
  ++a.cell.id;TickPart(&a,s,dt);assert(s.animation==0);
  a.leashed=false;TickPart(&a,s,dt);assert(!s.leashMoving); // Finish the existing leash-stop transition first.
  a.ridden=true;s.animation=4;TickPart(&a,s,dt);assert(s.animation==4&&!s.unriddenMotion.ready);
  a.ridden=false;a.bound=true;s.animation=7;TickPart(&a,s,dt);assert(s.animation==7&&!s.unriddenMotion.ready);
 }
 puts("PASS: actual unridden Tick branch at 30/60/144 FPS: zero-controller direct motion walks/runs/sprints, stationary/vertical/teleport reset, mounted/fixed isolation. Model test, not game QA.");
}
`);
