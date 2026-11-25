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
assert(!mount.includes('Travel::Remember'));
assert(/if\(t>=1.f\)\s*\{\s*if\(upActive\)\{\s*Travel::Remember\(mount.get\(\)\)/.test(source));
const waitStart=source.indexOf('if(upActive&&(!Crawl::Working(mount.get())||!Seat::Working()))');assert(waitStart>=0);
let waitEnd=source.indexOf('{',waitStart)+1,depth=1;while(depth){const c=source[waitEnd++];if(c==='{')depth++;if(c==='}')depth--;}
const wait=source.slice(waitStart,waitEnd);
const finishStart=source.indexOf('if(t>=1.f)',waitEnd);
const finishEnd=source.indexOf('\n        return;',finishStart);
const travel=fs.readFileSync(path.join(repo,'src/Travel.cpp'),'utf8');
const remember=travel.slice(travel.indexOf('void Remember('),travel.indexOf('void Forget('));
const save=travel.slice(travel.indexOf('void Save('),travel.indexOf('bool Load('));
fs.writeFileSync(path.join(repo,'build/mount-timing-regression.cpp'),`
#include <algorithm>
#include <cassert>
#include <cstdio>
#include "../src/TravelState.h"
namespace RE{struct Actor{enum class BOOL_FLAGS:unsigned{};unsigned id;unsigned GetFormID(){return id;}};}
namespace SKSE{struct SerializationInterface{unsigned value=0;void OpenRecord(unsigned tag,unsigned version){assert(tag=='TRVL'&&version==1);}void WriteRecordData(unsigned id){value=id;}};}
namespace spdlog{template<class... Args>void info(const char*,Args...){}}
namespace Travel{unsigned last=0;Tracker tracker;float probe=0;${remember}${save}}
bool upActive=true,unlocked=false;float transitionTime=0;unsigned notices=0;
struct Handle{RE::Actor actor{20};RE::Actor* get(){return &actor;}}mount;int player=0,config=0,downOffset=0;
namespace Crawl{bool ready=false;bool Working(RE::Actor*){return ready;}}
namespace Seat{bool ready=false;bool Working(){return ready;}void Motion(float,bool,int){}void Update(int,RE::Actor*,int,float=0,int=0){}void Entering(bool){}int WriteSequence(){return 0;}}
unsigned downFlags=0,downOriginalFlags=0,lastSeatWrite=0;float seatSilentTime=0;
namespace RE{namespace SendHUDMessage{void ShowHUDMessage(const char*){++notices;}}}
bool Dismount(bool emergency=false){assert(emergency);upActive=false;unlocked=true;transitionTime=0;return true;}
void TickWait(float delta){${wait}transitionTime=std::max(transitionTime,0.f);transitionTime+=delta;
 const float t=std::clamp(transitionTime/.85f,0.f,1.f);
 struct{void set(RE::Actor::BOOL_FLAGS){}void reset(unsigned){}}playerFlags;
 ${source.slice(finishStart,finishEnd)}
}
unsigned SavedMount(){SKSE::SerializationInterface record;Travel::Save(&record);return record.value;}
int main(){for(float fps:{30.f,60.f,144.f}){
 RE::Actor previous{10};Travel::Remember(&previous);upActive=true;unlocked=false;transitionTime=0;Crawl::ready=Seat::ready=false;const auto before=notices;
 for(int i=0;i<int(fps*3.2f)&&upActive;++i)TickWait(1/fps);
 assert(!upActive&&unlocked&&notices==before+1&&SavedMount()==previous.id);
 upActive=true;unlocked=false;transitionTime=0;Crawl::ready=Seat::ready=false;
 for(int i=0;i<int(fps);++i)TickWait(1/fps);assert(upActive&&!unlocked&&transitionTime<0);
 Crawl::ready=Seat::ready=true;TickWait(1/fps);assert(upActive&&!unlocked&&transitionTime==1/fps);
 assert(SavedMount()==previous.id);
 for(int i=0;i<int(fps)&&upActive;++i)TickWait(1/fps);
 assert(!upActive&&SavedMount()==mount.get()->id);
 }std::puts("PASS: actual mount wait branch releases unavailable output after 3s and resumes from zero when ready, 30/60/144 FPS. Not game QA.");}
`);
