// Executes actual Restore/RestoreAll/Save/Load bodies against engine test doubles.
const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/plugin.cpp'),'utf8');
function extract(signature) {
 const start=source.indexOf(signature);assert(start>=0,signature);
 const open=source.indexOf('{',start);let end=open+1,depth=1;
 while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}
 return source.slice(start,end);
}
const code=`
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>
#include "../src/Config.h"
namespace RE {
using FormID=std::uint32_t;
enum class ACTOR_LIFE_STATE : std::uint32_t { kAlive=0,kDead=2,kRestrained=6 };
enum class ActorValue{kSpeedMult};enum class ACTOR_VALUE_MODIFIER{kTemporary};
struct Flags {
 std::uint32_t value{};
 template<class T> void reset(T mask){value&=~static_cast<unsigned>(mask);}
 template<class T> void set(T mask){value|=static_cast<unsigned>(mask);}
};
struct Actor {
 enum class BOOL_FLAGS : unsigned {};
 FormID id; bool enabled=true,dead=false,marker=true,inScene=false,ready=true; unsigned evaluations{};
 float scale=1.24f,speed=100;unsigned scaleWrites=0;bool quantizeScale=false;
 float raceHeight=1;struct Reference{float refScale;} reference;
 auto& GetReferenceRuntimeData(){reference.refScale=scale/raceHeight*100;return reference;}
 float GetScale(){return scale;}void SetScale(float value){++scaleWrites;scale=(quantizeScale?std::round(value*100)/100:value)*raceHeight;}
 Actor* AsActorValueOwner(){return this;}float GetActorValue(ActorValue){return speed;}
 void ModActorValue(ACTOR_VALUE_MODIFIER,ActorValue,float delta){speed+=delta;}
 ACTOR_LIFE_STATE life=ACTOR_LIFE_STATE::kRestrained;
 struct { Flags boolFlags; } data;
 std::vector<std::string> events;
 FormID GetFormID(){return id;} bool IsDead(){return dead;}
 void* GetCurrentScene(){return inScene?this:nullptr;}
 bool IsInKillMove(){return inScene;}
 Actor* AsActorState(){return this;} auto GetLifeState(){return life;}
 void SetLifeState(ACTOR_LIFE_STATE state){life=state;}
 auto& GetActorRuntimeData(){return data;}
 void RemoveFromFaction(void*){marker=false;}
 bool NotifyAnimationGraph(const char* event){
   if(std::strcmp(event,"IdleForceDefaultState")==0)assert(!marker);
   events.emplace_back(event);return true;
 }
 void EnableAI(bool value){enabled=value;}
 void EvaluatePackage(bool,bool){++evaluations;}
};
inline std::unordered_map<FormID,Actor*> loaded;
struct TESForm { template<class T> static T* LookupByID(FormID id){auto it=loaded.find(id);return it==loaded.end()?nullptr:it->second;} };
}
namespace SKSE {
struct SerializationInterface {
 std::uint32_t type{},version{};std::vector<char> bytes;std::size_t cursor{};bool read{};
 struct Record{std::uint32_t type,version;std::vector<char> bytes;};std::vector<Record> previous;std::size_t recordIndex=0;const std::vector<char>* reading=nullptr;
 bool OpenRecord(std::uint32_t t,std::uint32_t v){if(type)previous.push_back({type,version,bytes});type=t;version=v;bytes.clear();cursor=0;read=false;reading=nullptr;return true;}
 template<class T> bool WriteRecordData(const T& value){const char* p=reinterpret_cast<const char*>(&value);bytes.insert(bytes.end(),p,p+sizeof(T));return true;}
 bool GetNextRecordInfo(std::uint32_t& t,std::uint32_t& v,std::uint32_t& length){
  if(!read){recordIndex=0;read=true;}if(recordIndex>previous.size())return false;
  if(recordIndex<previous.size()){const auto& r=previous[recordIndex];t=r.type;v=r.version;reading=&r.bytes;}else{t=type;v=version;reading=&bytes;}
  ++recordIndex;cursor=0;length=static_cast<std::uint32_t>(reading->size());return true;
 }
 unsigned ReadRecordData(void* out,unsigned size){auto n=std::min<std::size_t>(size,reading->size()-cursor);std::memcpy(out,reading->data()+cursor,n);cursor+=n;return static_cast<unsigned>(n);}
 bool ResolveFormID(std::uint32_t id,std::uint32_t& out){out=id+0x10000000;return id!=0;}
};
}
namespace spdlog { template<class... T> void info(const char*,T...){} }
namespace Extras {
bool ready=true;bool Release(RE::Actor*){return ready;}void ReleaseAll(){}
void Save(SKSE::SerializationInterface*){}bool Load(SKSE::SerializationInterface*,std::uint32_t,std::uint32_t,std::uint32_t){return false;}
}
namespace Wardrobe {void Restore(RE::Actor*){}void Save(SKSE::SerializationInterface*){}bool Load(SKSE::SerializationInterface*,std::uint32_t,std::uint32_t,std::uint32_t){return false;}}
constexpr auto ownedFlags=static_cast<RE::Actor::BOOL_FLAGS>((1u<<27)|(1u<<20)|(1u<<21)|(1u<<29));
${extract('struct State {')};
std::unordered_map<RE::FormID,State> targets;
void* mountFaction{};
struct { std::shared_ptr<RE::Actor> value;auto get(){return value;}explicit operator bool()const{return bool(value);} } ridden;
namespace Crawl{void Remove(unsigned){}}
namespace Travel{void Forget(unsigned){}void Save(SKSE::SerializationInterface*){}bool Load(SKSE::SerializationInterface*,unsigned,unsigned,unsigned){return false;}}
Settings::Data config;float playerSpeedDelta=0;std::uint32_t playerControllerFlags=0,downOriginalFlags=0;bool downActive=false,loadedRideState=false,upActive=false;
bool allowDismount=true;int dismountCalls{};
int downRequests=0;void BeginDismount(){++downRequests;}
bool Dismount(bool emergency=false,bool notify=true){++dismountCalls;if(allowDismount||emergency)ridden.value.reset();return allowDismount||emergency;}
bool Converted(RE::Actor* actor){return actor&&targets.contains(actor->id);}
bool ActorReady(RE::Actor* actor){return actor->ready;}
${extract('void ApplySpeed(')}
${extract('void RestoreScale(')}
${extract('void ApplyStats(')}
${extract('void StopMotion(')}
${extract('void Restore(')}
${extract('void RestoreAll(')}
${extract('void Save(')}
${extract('void Load(')}
void prepare(RE::Actor& actor,bool ai=true,RE::ACTOR_LIFE_STATE life=RE::ACTOR_LIFE_STATE::kAlive){
 actor.data.boolFlags.value=static_cast<unsigned>(ownedFlags)|(1u<<18);
 targets[actor.id]=State{1u<<20,3,ai,life};RE::loaded[actor.id]=&actor;
}
int main(){
 RE::Actor normal{1};prepare(normal);Restore(&normal);
 assert(!targets.contains(1)&&!normal.marker&&normal.enabled&&normal.evaluations==1);
 assert(normal.life==RE::ACTOR_LIFE_STATE::kAlive);
 assert(normal.data.boolFlags.value==((1u<<18)|(1u<<20)));
 assert(normal.events.size()==3&&normal.events[0]=="SprintStop"&&normal.events[1]=="moveStop");
 RE::Actor deferred{16};prepare(deferred);deferred.ready=false;Restore(&deferred);assert(targets.at(16).restoreRequested&&deferred.marker&&deferred.events.empty());deferred.ready=true;Restore(&deferred);assert(!targets.contains(16)&&!deferred.marker);
 RE::Actor disabled{2};prepare(disabled,false,RE::ACTOR_LIFE_STATE::kRestrained);Restore(&disabled);
 assert(!disabled.enabled&&disabled.evaluations==0&&disabled.life==RE::ACTOR_LIFE_STATE::kRestrained);
 RE::Actor tethered{14};prepare(tethered);Extras::ready=false;Restore(&tethered);assert(targets.at(14).restoreRequested&&tethered.marker);
 Extras::ready=true;Restore(&tethered);assert(!targets.contains(14)&&!tethered.marker);
 RE::Actor priorRestrained{15};prepare(priorRestrained,false,RE::ACTOR_LIFE_STATE::kRestrained);
 priorRestrained.life=RE::ACTOR_LIFE_STATE::kAlive; // E temporarily released an originally restrained actor.
 Restore(&priorRestrained);assert(priorRestrained.life==RE::ACTOR_LIFE_STATE::kRestrained&&!priorRestrained.enabled&&!targets.contains(15));
 RE::Actor dead{3};dead.dead=true;prepare(dead);Restore(&dead);
 assert(dead.dead&&dead.life==RE::ACTOR_LIFE_STATE::kRestrained&&dead.events.size()==2);
 auto air=std::make_shared<RE::Actor>(RE::Actor{4});prepare(*air);ridden.value=air;allowDismount=false;
 Restore(air.get());assert(targets.at(4).restoreRequested&&air->marker&&air->life==RE::ACTOR_LIFE_STATE::kRestrained);
 assert(downRequests==1);ridden.value.reset();Restore(air.get());assert(!targets.contains(4)&&!air->marker&&air->life==RE::ACTOR_LIFE_STATE::kAlive);
 for(bool death : {false,true}) {
   auto emergency=std::make_shared<RE::Actor>(RE::Actor{8});prepare(*emergency);
   emergency->dead=death;emergency->inScene=!death;ridden.value=emergency;allowDismount=false;
   Restore(emergency.get());assert(!ridden.value&&!targets.contains(8)&&!emergency->marker);
 }
 allowDismount=true;
 RE::Actor loaded{5},unloaded{6};prepare(loaded);prepare(unloaded);RE::loaded.erase(6);
 RestoreAll();assert(!targets.contains(5)&&targets.at(6).restoreRequested);
 RE::loaded[6]=&unloaded;Restore(&unloaded);assert(targets.empty());
 // Actual Load: old version migrations, original flag masking and deferred request.
 for(unsigned version=1;version<=5;++version){
   SKSE::SerializationInterface s;s.OpenRecord('NMNT',version);
   s.WriteRecordData(1u);s.WriteRecordData(42u);s.WriteRecordData((1u<<18)|(1u<<20));
   if(version>=2)s.WriteRecordData(0u);
   if(version>=3)s.WriteRecordData(6u);
   if(version>=4)s.WriteRecordData(1u);
   if(version==5){s.WriteRecordData(1.24f);s.WriteRecordData(50.f);s.WriteRecordData(1u);}
   targets.clear();Load(&s);assert(targets.size()==1);auto state=targets.at(0x1000002A);
   assert(state.originalFlags==(1u<<20)&&state.aiEnabled==(version==1));
   assert(state.lifeState==(version>=3?RE::ACTOR_LIFE_STATE::kRestrained:RE::ACTOR_LIFE_STATE::kAlive));
   assert(state.restoreRequested==(version>=4));
   if(version==5)assert(state.originalScale==1.24f&&state.speedDelta==50&&state.scaleChanged);
 }
 targets.clear();targets.emplace(7,State{1u<<20,3,false,RE::ACTOR_LIFE_STATE::kRestrained,true});
 SKSE::SerializationInterface roundtrip;Save(&roundtrip);assert(roundtrip.version==5&&roundtrip.bytes.size()==36);
 targets.clear();Load(&roundtrip);assert(targets.at(0x10000007).restoreRequested&&!targets.at(0x10000007).aiEnabled);
 roundtrip.read=false;roundtrip.cursor=0;roundtrip.bytes.pop_back();targets.clear();Load(&roundtrip);assert(targets.empty());
 targets.clear();RE::Actor scaled{9};prepare(scaled);config.overrideScale=true;config.npcScale=3;config.speedMultiplier=2;
 ApplyStats(&scaled,targets.at(9));assert(scaled.scale==3&&scaled.speed==200&&targets.at(9).originalScale==1.24f);
 for(int i=0;i<1000;i++)ApplyStats(&scaled,targets.at(9));assert(scaled.speed==200&&scaled.scaleWrites==1);
 scaled.speed+=20;ApplyStats(&scaled,targets.at(9));assert(scaled.speed==240); // another modifier is preserved, and multiplied
 Restore(&scaled);assert(scaled.scale==1.24f&&scaled.speed==120);
 RE::Actor quantized{13};quantized.quantizeScale=true;prepare(quantized);config.npcScale=1.234567f;
 for(int i=0;i<1000;i++)ApplyStats(&quantized,targets.at(13));assert(quantized.scaleWrites==1);
 config.npcScale=.5f;ApplyStats(&quantized,targets.at(13));assert(quantized.scaleWrites==2&&quantized.scale==.5f);
 config.overrideScale=false;ApplyStats(&quantized,targets.at(13));assert(quantized.scaleWrites==3&&quantized.scale==1.24f);
 Restore(&quantized);assert(quantized.scaleWrites==3&&quantized.speed==100);config.overrideScale=true;config.npcScale=3;
 // Recorded game case: GetScale=1.03, SetScale(1.03) produced 1.0609.
 for(float height:{.95f,1.03f,1.08f}){
  RE::Actor racial{99};racial.raceHeight=height;racial.scale=height;racial.quantizeScale=true;
  for(int i=0;i<10;++i){prepare(racial);config.npcScale=1.9f;ApplyStats(&racial,targets.at(99));Restore(&racial);assert(std::abs(racial.scale-height)<.00001f);}
  prepare(racial);ApplyStats(&racial,targets.at(99));config.overrideScale=false;ApplyStats(&racial,targets.at(99));assert(std::abs(racial.scale-height)<.00001f);Restore(&racial);config.overrideScale=true;
 }
 // Below-threshold adjustments must not change bookkeeping without an AV write.
 RE::Actor micro{11};float ledger=0;
 for(int i=0;i<1000;++i){ApplySpeed(&micro,1.000005f,ledger);assert(std::abs((micro.speed-ledger)-100.f)<.00002f);}
 ApplySpeed(&micro,2.f,ledger);ApplySpeed(&micro,1.f,ledger);assert(std::abs(micro.speed-100.f)<.00002f&&ledger==0);
 micro.speed=100.0005f;ledger=.0005f;ApplySpeed(&micro,1.f,ledger);assert(std::abs(micro.speed-100.f)<.00002f&&ledger==0);
 RE::Actor reload{10};prepare(reload);ApplyStats(&reload,targets.at(10));SKSE::SerializationInterface persisted;Save(&persisted);
 targets.clear();Load(&persisted);reload.id=0x1000000A;Restore(&reload);assert(reload.scale==1.24f&&reload.speed==100);
 SKSE::SerializationInterface rideRecord;rideRecord.OpenRecord('RIDE',1);rideRecord.WriteRecordData(75.f);rideRecord.WriteRecordData(42u);rideRecord.WriteRecordData(1u);rideRecord.WriteRecordData(7u);
 Load(&rideRecord);assert(loadedRideState&&playerSpeedDelta==75&&playerControllerFlags==42&&downActive&&downOriginalFlags==7);
 auto mounted=std::make_shared<RE::Actor>(RE::Actor{12});prepare(*mounted);ridden.value=mounted;
 ApplyStats(mounted.get(),targets.at(12));SKSE::SerializationInterface both;Save(&both);
 assert(both.previous.size()==1&&both.previous[0].type=='NMNT'&&both.type=='RIDE');
 targets.clear();ridden.value.reset();playerSpeedDelta=0;downActive=loadedRideState=false;Load(&both);
 assert(targets.contains(0x1000000C)&&targets.at(0x1000000C).scaleChanged&&loadedRideState&&downActive&&playerSpeedDelta==75);
 mounted->id=0x1000000C;Restore(mounted.get());assert(mounted->scale==1.24f&&mounted->speed==100);
 std::puts("PASS: B queues animated dismount; emergency release; v1-v5 migration; scale/speed non-stacking and other modifiers preserved; v5 restore after load; RIDE recovery record. Engine doubles, not game QA.");
}
`;
fs.writeFileSync(path.join(repo,'build/restore-regression.cpp'),code);
console.log('Generated regression from actual Restore/RestoreAll/Save/Load bodies.');
