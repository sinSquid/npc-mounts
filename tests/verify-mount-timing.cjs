const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/plugin.cpp'),'utf8');
assert(source.includes('constexpr float transitionSideOffset=128.f'));
const mount=source.slice(source.indexOf('void Mount('),source.indexOf('void Tick('));
assert(mount.includes('upActive=true;transitionTime=0.f'));
assert(mount.includes('Seat::Entering(true);Seat::Update(player,actor,config,1.f,downOffset)'));
assert(mount.includes('ResetPose(actor,targets.at(actor->GetFormID()))'));
assert(mount.includes('cc->outVelocity=RE::hkVector4()')&&mount.includes('proxy->outVelocity=RE::hkVector4()'));
assert(mount.includes('std::cos(angle)*transitionSideOffset')&&mount.includes('-std::sin(angle)*transitionSideOffset'));
assert(!mount.includes('NotifyAnimationGraph("Mount')&&!mount.includes('NotifyAnimationGraph("Horse'));
const waitStart=source.indexOf('if(upActive&&(!Crawl::Working(mount.get())||!Seat::Working()))');assert(waitStart>=0);
let waitEnd=source.indexOf('{',waitStart)+1,depth=1;while(depth){const c=source[waitEnd++];if(c==='{')depth++;if(c==='}')depth--;}
const wait=source.slice(waitStart,waitEnd);
fs.writeFileSync(path.join(repo,'build/mount-timing-regression.cpp'),`
#include <algorithm>
#include <cassert>
#include <cstdio>
bool upActive=true,unlocked=false;float transitionTime=0;unsigned notices=0;
struct Handle{int* get(){return nullptr;}}mount;int player=0,config=0,downOffset=0;
namespace Crawl{bool ready=false;bool Working(int*){return ready;}}
namespace Seat{bool ready=false;bool Working(){return ready;}void Motion(float,bool,int){}void Update(int,int*,int,float,int){}}
namespace RE{namespace SendHUDMessage{void ShowHUDMessage(const char*){++notices;}}}
bool Dismount(bool emergency){assert(emergency);upActive=false;unlocked=true;transitionTime=0;return true;}
void TickWait(float delta){${wait}transitionTime=std::max(transitionTime,0.f);transitionTime+=delta;}
int main(){for(float fps:{30.f,60.f,144.f}){
 upActive=true;unlocked=false;transitionTime=0;Crawl::ready=Seat::ready=false;const auto before=notices;
 for(int i=0;i<int(fps*3.2f)&&upActive;++i)TickWait(1/fps);
 assert(!upActive&&unlocked&&notices==before+1);
 upActive=true;unlocked=false;transitionTime=0;Crawl::ready=Seat::ready=false;
 for(int i=0;i<int(fps);++i)TickWait(1/fps);assert(upActive&&!unlocked&&transitionTime<0);
 Crawl::ready=Seat::ready=true;TickWait(1/fps);assert(upActive&&!unlocked&&transitionTime==1/fps);
 }std::puts("PASS: actual mount wait branch releases unavailable output after 3s and resumes from zero when ready, 30/60/144 FPS. Not game QA.");}
`);
