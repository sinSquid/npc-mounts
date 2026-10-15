const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/plugin.cpp'),'utf8');
function block(marker){const start=source.indexOf(marker,source.indexOf('void Tick('));assert(start>=0);let end=source.indexOf('{',start)+1,n=1;while(n){const c=source[end++];if(c==='{')n++;if(c==='}')n--;}return source.slice(start,end);}
fs.writeFileSync(path.join(repo,'build/mount-timing-regression.cpp'),`
#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <cstdio>
namespace RE{struct hkVector4{struct{float m128_f32[4]{};}quad;};struct Actor{enum class BOOL_FLAGS:unsigned{blocked=3};struct Flags{unsigned value=0;void reset(BOOL_FLAGS x){value&=~unsigned(x);}void set(BOOL_FLAGS x){value|=unsigned(x);}};struct{Flags boolFlags;}data;struct Controller{hkVector4 v;void GetLinearVelocityImpl(hkVector4& out){out=v;}void SetLinearVelocityImpl(hkVector4 in){v=in;}}cc;auto* GetCharController(){return &cc;}auto& GetActorRuntimeData(){return data;}unsigned GetFormID(){return 1;}};}
namespace Seat{bool finished=false,entering=true;float phase=1;bool Finished(){return finished;}void Entering(bool v){entering=v;}void Update(RE::Actor*,RE::Actor*,int,float p=-1,int=0){phase=p;}}
namespace Crawl{void Update(RE::Actor*,int,float){}}
bool upActive=true,wasAir=false;float upElapsed=0,upDuration=.85f,jumpTimer=.2f;int config=0,upOffset=0,begins=0;unsigned downOriginalFlags=2;auto downFlags=RE::Actor::BOOL_FLAGS::blocked;
struct State{bool restoreRequested=false;};std::unordered_map<unsigned,State> targets{{1,{}}};void BeginDismount(){++begins;}
void tick(RE::Actor* player,std::shared_ptr<RE::Actor> mount,float delta){${block('    if(upActive){')}}
void jump(RE::Actor::Controller* cc,bool air,float multiplier){auto velocity=cc->v;struct{float jumpHeightMultiplier;}config{multiplier};${block('    if(air&&!wasAir')}}
int main(){for(float fps:{30.f,60.f,144.f})for(bool restore:{false,true}){RE::Actor p;auto m=std::make_shared<RE::Actor>();upActive=true;upElapsed=0;begins=0;Seat::finished=false;Seat::phase=1;Seat::entering=true;targets[1]={restore};p.data.boolFlags.value=19;p.cc.v.quad.m128_f32[0]=4;p.cc.v.quad.m128_f32[2]=-2;
 while(Seat::phase>0)tick(&p,m,1/fps);assert(upActive&&begins==0);tick(&p,m,1/fps);assert(upActive);assert(p.cc.v.quad.m128_f32[0]==0&&p.cc.v.quad.m128_f32[2]==-2);Seat::finished=true;tick(&p,m,1/fps);assert(!upActive&&!Seat::entering&&p.data.boolFlags.value==18&&begins==int(restore));}
 RE::Actor p;auto m=std::make_shared<RE::Actor>();upActive=true;upElapsed=0;Seat::finished=false;for(int i=0;i<100&&upActive;i++)tick(&p,m,.02f);assert(!upActive&&upElapsed<1.2f);
 for(float coefficient:{.8f,1.f,1.5f,2.f}){p.cc.v.quad.m128_f32[2]=4;wasAir=false;jumpTimer=.2f;jump(&p.cc,true,coefficient);assert(std::abs(p.cc.v.quad.m128_f32[2]*p.cc.v.quad.m128_f32[2]/16-coefficient)<.0001f);auto v=p.cc.v;wasAir=true;jump(&p.cc,true,coefficient);assert(p.cc.v.quad.m128_f32[2]==v.quad.m128_f32[2]);wasAir=false;jumpTimer=0;jump(&p.cc,true,coefficient);assert(p.cc.v.quad.m128_f32[2]==v.quad.m128_f32[2]);}
 std::puts("PASS: actual mount transition at 30/60/144 FPS, reverse final-pose acknowledgement, queued restore, timeout, original flags and one-shot jump height multiplier.");}
`);
