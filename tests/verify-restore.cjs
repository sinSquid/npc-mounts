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
namespace RE {
using FormID=std::uint32_t;
enum class ACTOR_LIFE_STATE : std::uint32_t { kAlive=0,kDead=2,kRestrained=6 };
struct Flags {
 std::uint32_t value{};
 template<class T> void reset(T mask){value&=~static_cast<unsigned>(mask);}
 template<class T> void set(T mask){value|=static_cast<unsigned>(mask);}
};
struct Actor {
 enum class BOOL_FLAGS : unsigned {};
 FormID id; bool enabled=true,dead=false,marker=true,inScene=false; unsigned evaluations{};
 ACTOR_LIFE_STATE life=ACTOR_LIFE_STATE::kRestrained;
 struct { Flags boolFlags; } data;
 std::vector<std::string> events;
 FormID GetFormID(){return id;} bool IsDead(){return dead;}
 void* GetCurrentScene(){return inScene?this:nullptr;}
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
 bool OpenRecord(std::uint32_t t,std::uint32_t v){type=t;version=v;bytes.clear();cursor=0;read=false;return true;}
 template<class T> bool WriteRecordData(const T& value){const char* p=reinterpret_cast<const char*>(&value);bytes.insert(bytes.end(),p,p+sizeof(T));return true;}
 bool GetNextRecordInfo(std::uint32_t& t,std::uint32_t& v,std::uint32_t& length){if(read)return false;read=true;t=type;v=version;length=static_cast<std::uint32_t>(bytes.size());return true;}
 unsigned ReadRecordData(void* out,unsigned size){auto n=std::min<std::size_t>(size,bytes.size()-cursor);std::memcpy(out,bytes.data()+cursor,n);cursor+=n;return static_cast<unsigned>(n);}
 bool ResolveFormID(std::uint32_t id,std::uint32_t& out){out=id+0x10000000;return id!=0;}
};
}
namespace spdlog { template<class... T> void info(const char*,T...){} }
constexpr auto ownedFlags=static_cast<RE::Actor::BOOL_FLAGS>((1u<<27)|(1u<<20)|(1u<<21)|(1u<<29));
${extract('struct State {')};
std::unordered_map<RE::FormID,State> targets;
void* mountFaction{};
struct { std::shared_ptr<RE::Actor> value;auto get(){return value;} } ridden;
bool allowDismount=true;int dismountCalls{};
bool Dismount(bool emergency=false,bool notify=true){++dismountCalls;if(allowDismount||emergency)ridden.value.reset();return allowDismount||emergency;}
bool Converted(RE::Actor* actor){return actor&&targets.contains(actor->id);}
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
 RE::Actor disabled{2};prepare(disabled,false,RE::ACTOR_LIFE_STATE::kRestrained);Restore(&disabled);
 assert(!disabled.enabled&&disabled.evaluations==0&&disabled.life==RE::ACTOR_LIFE_STATE::kRestrained);
 RE::Actor dead{3};dead.dead=true;prepare(dead);Restore(&dead);
 assert(dead.dead&&dead.life==RE::ACTOR_LIFE_STATE::kRestrained&&dead.events.size()==2);
 auto air=std::make_shared<RE::Actor>(RE::Actor{4});prepare(*air);ridden.value=air;allowDismount=false;
 Restore(air.get());assert(targets.at(4).restoreRequested&&air->marker&&air->life==RE::ACTOR_LIFE_STATE::kRestrained);
 allowDismount=true;Restore(air.get());assert(!targets.contains(4)&&!air->marker&&air->life==RE::ACTOR_LIFE_STATE::kAlive);
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
 for(unsigned version=1;version<=4;++version){
   SKSE::SerializationInterface s;s.OpenRecord('NMNT',version);
   s.WriteRecordData(1u);s.WriteRecordData(42u);s.WriteRecordData((1u<<18)|(1u<<20));
   if(version>=2)s.WriteRecordData(0u);
   if(version>=3)s.WriteRecordData(6u);
   if(version>=4)s.WriteRecordData(1u);
   targets.clear();Load(&s);assert(targets.size()==1);auto state=targets.at(0x1000002A);
   assert(state.originalFlags==(1u<<20)&&state.aiEnabled==(version==1));
   assert(state.lifeState==(version>=3?RE::ACTOR_LIFE_STATE::kRestrained:RE::ACTOR_LIFE_STATE::kAlive));
   assert(state.restoreRequested==(version==4));
 }
 targets.clear();targets.emplace(7,State{1u<<20,3,false,RE::ACTOR_LIFE_STATE::kRestrained,true});
 SKSE::SerializationInterface roundtrip;Save(&roundtrip);assert(roundtrip.version==4&&roundtrip.bytes.size()==24);
 targets.clear();Load(&roundtrip);assert(targets.at(0x10000007).restoreRequested&&!targets.at(0x10000007).aiEnabled);
 roundtrip.read=false;roundtrip.cursor=0;roundtrip.bytes.pop_back();targets.clear();Load(&roundtrip);assert(targets.empty());
 std::puts("PASS: actual B restoration, deferred landing, unloaded targets, SprintStop, original flags/AI/life, dead actor safety, actual v1-v4 load and v4 save/load. Engine test doubles, not game QA.");
}
`;
fs.writeFileSync(path.join(repo,'build/restore-regression.cpp'),code);
console.log('Generated regression from actual Restore/RestoreAll/Save/Load bodies.');
