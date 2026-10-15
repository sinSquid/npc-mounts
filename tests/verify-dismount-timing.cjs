// Execute the actual Tick dismount branch, including its return order.
const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/plugin.cpp'),'utf8');
const start=source.indexOf('    if(downActive){',source.indexOf('void Tick('));assert(start>=0);
let end=source.indexOf('{',start)+1,depth=1;while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}
fs.writeFileSync(path.join(repo,'build/dismount-timing-regression.cpp'),`
#include <algorithm>
#include <cassert>
#include <memory>
#include <unordered_map>
#include <cstdio>
namespace RE{struct hkVector4{struct{float m128_f32[4]{};}quad;};}
struct Controller{RE::hkVector4 v;void GetLinearVelocityImpl(RE::hkVector4& out){out=v;}void SetLinearVelocityImpl(RE::hkVector4 in){v=in;}};
struct Actor{Controller cc;Controller* GetCharController(){return &cc;}unsigned GetFormID(){return 1;}};
namespace Seat{bool finished=false;float phase=-1;bool Finished(){return finished;}void Update(Actor*,Actor*,int,float p,int){phase=p;}}
namespace Crawl{void Update(Actor*,int,float){}}
namespace spdlog{int warnings=0;void warn(const char*){++warnings;}}
bool downActive=true;float downElapsed=0,downDuration=.85f;int config=0,downOffset=0,dismounts=0,restores=0;
struct State{bool restoreRequested=false;};std::unordered_map<unsigned,State> targets{{1,{}}};
void Dismount(){++dismounts;downActive=false;}void Restore(Actor*){++restores;targets.erase(1);}
void tick(Actor* player,std::shared_ptr<Actor> mount,float delta){${source.slice(start,end)}}
int main(){
 for(float fps:{30.f,60.f,144.f}){
  Actor player;auto mount=std::make_shared<Actor>();downActive=true;downElapsed=0;dismounts=restores=0;Seat::finished=false;Seat::phase=-1;targets[1]={true};
  player.cc.v.quad.m128_f32[0]=4;player.cc.v.quad.m128_f32[1]=5;player.cc.v.quad.m128_f32[2]=-2;
  while(Seat::phase<1)tick(&player,mount,1/fps);
  assert(dismounts==0&&restores==0&&targets.contains(1));
  tick(&player,mount,1/fps);assert(dismounts==0); // elapsed alone is not a pose acknowledgement
  assert(player.cc.v.quad.m128_f32[0]==0&&player.cc.v.quad.m128_f32[1]==0&&player.cc.v.quad.m128_f32[2]==-2);
  Seat::finished=true;tick(&player,mount,1/fps);assert(dismounts==1&&restores==1&&!targets.contains(1));
 }
 Actor player;auto mount=std::make_shared<Actor>();downActive=true;downElapsed=0;dismounts=restores=0;Seat::finished=false;targets[1]={false};
 for(int i=0;i<100&&downActive;++i)tick(&player,mount,.02f);
 assert(!downActive&&dismounts==1&&restores==0&&spdlog::warnings==1&&downElapsed<1.2f);
 std::puts("PASS: actual dismount Tick branch waits for output at 30/60/144 FPS, B restores after completion, missing graph timeout releases, vertical velocity preserved.");
}
`);
