// Executes production pin/hostility hooks and BIND/TETH save/load with engine doubles.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'src/Extras.cpp'),'utf8');
function take(sig){let start=source.indexOf(sig);assert(start>=0,sig);let end=source.indexOf('{',start)+1,depth=1;while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}return source.slice(start,end);}
fs.writeFileSync(path.join(root,'build/restraint-hooks-regression.cpp'),`
#include <array>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <functional>
namespace RE {
using FormID=std::uint32_t;struct NiPoint3{float x=0,y=0,z=0;bool operator==(const NiPoint3&)const=default;};
struct ObjectRefHandle{};struct TESWorldSpace{};struct CombatGroup{};struct hkVector4{};
struct TESObjectCELL{struct{TESWorldSpace* worldSpace=nullptr;}data;auto& GetRuntimeData(){return data;}};
inline TESObjectCELL cell;
struct TESForm{template<class T>static T* LookupByID(FormID id){return id==2?reinterpret_cast<T*>(&cell):nullptr;}};
struct TESObjectREFR{FormID id=1;FormID GetFormID(){return id;}};
struct Actor:TESObjectREFR{enum class BOOL_FLAGS:unsigned{};NiPoint3 pos,angle;TESObjectCELL* parent=&cell;bool loaded=true;
 struct Controller{int zeroes=0;void SetLinearVelocityImpl(hkVector4){++zeroes;}}cc;
 TESObjectCELL* GetParentCell(){return parent;}void* Get3D(){return loaded?this:nullptr;}auto GetPosition(){return pos;}auto GetAngle(){return angle;}void SetAngle(NiPoint3 a){angle=a;}auto* GetCharController(){return &cc;}
};
}
namespace SKSE {struct SerializationInterface{
 bool resolveCells=true;
 struct Record{unsigned type,version;std::vector<char> bytes;};std::vector<Record> records;std::size_t index=0,cursor=0;
 void OpenRecord(unsigned t,unsigned v){records.push_back({t,v,{}});}template<class T>void WriteRecordData(T value){auto* data=reinterpret_cast<char*>(&value);auto& b=records.back().bytes;b.insert(b.end(),data,data+sizeof(T));}
 unsigned ReadRecordData(void* out,unsigned n){auto& b=records.at(index).bytes;auto count=std::min<std::size_t>(n,b.size()-cursor);std::memcpy(out,b.data()+cursor,count);cursor+=count;return static_cast<unsigned>(count);}
 bool ResolveFormID(unsigned old,unsigned& id){if(!old||(!resolveCells&&old==2))return false;id=old;return true;}
};}
constexpr auto flagsMask=static_cast<RE::Actor::BOOL_FLAGS>((1u<<27)|(1u<<20)|(1u<<21));
${take('struct Binding {')};
${take('enum class Phase ')};${take('struct Tether {')};
std::atomic_uint64_t leashEventsSeen{0};
std::mutex mutex;std::unordered_map<RE::FormID,Binding> bindings;std::unordered_map<RE::FormID,Tether> tethers;std::uint64_t serial=0;
int moves=0,positions=0,combats=0;bool failMove=false;std::function<void()> reenter;
void originalMove(RE::TESObjectREFR* base,const RE::ObjectRefHandle&,RE::TESObjectCELL* cell,RE::TESWorldSpace*,const RE::NiPoint3& pos,const RE::NiPoint3& angle){++moves;if(reenter)reenter();if(failMove)return;auto* actor=static_cast<RE::Actor*>(base);actor->parent=cell;actor->pos=pos;actor->angle=angle;}
void originalPosition(RE::Actor* actor,const RE::NiPoint3& pos,bool){++positions;actor->pos=pos;}
bool originalHostile(RE::Actor*,RE::Actor*){return true;}bool originalCombat(RE::Actor*,RE::Actor*,RE::CombatGroup*){++combats;return true;}
${take('bool Find(')}
${take('bool Pacified(')}
${take('void MoveHook(')}
${take('void PositionHook(')}
${take('bool HostileHook(')}
${take('bool CombatHook(')}
${take('void Freeze(')}
${take('void Save(')}
${take('bool Load(')}
int main(){
 RE::Actor actor,other;other.id=99;RE::TESObjectCELL elsewhere;RE::NiPoint3 destination{100,200,300};RE::ObjectRefHandle handle;
 Binding pin;pin.position={1,2,3};pin.angle={0,0,.5f};pin.cell=2;pin.post=3;pin.oldHands=4;pin.flags=static_cast<unsigned>(flagsMask);pin.life=6;pin.releaseRequested=true;bindings[1]=pin;
 for(bool loaded:{true,false}){actor.loaded=loaded;MoveHook(&actor,handle,&elsewhere,nullptr,destination,{});assert(moves==0&&actor.parent==&RE::cell);}
 PositionHook(&actor,destination,true);assert(actor.pos==pin.position&&positions==1);
 MoveHook(&other,handle,&elsewhere,nullptr,destination,{});assert(moves==1&&other.parent==&elsewhere);
 actor.parent=&elsewhere;actor.pos=destination;actor.loaded=true;Freeze(&actor,pin);assert(actor.parent==&RE::cell&&actor.pos==pin.position&&actor.angle==pin.angle&&moves==2&&actor.cc.zeroes==1);
 assert(HostileHook(&actor,&other)&&CombatHook(&actor,&other,nullptr));
 bindings[1].pacified=true;assert(!HostileHook(&actor,&other)&&!HostileHook(&other,&actor));
 assert(!CombatHook(&actor,&other,nullptr)&&!CombatHook(&other,&actor,nullptr)&&combats==1);
 MoveHook(&actor,handle,&elsewhere,nullptr,destination,{});assert(moves==3); // D is not a C teleport anchor
 bindings[1].pacified=false;tethers[9]={Phase::Active,false,5};
 SKSE::SerializationInterface save;Save(&save);assert(save.records.size()==2&&save.records[0].bytes.size()==68&&save.records[1].bytes.size()==12);
 bindings.clear();tethers.clear();
 for(save.index=0;save.index<save.records.size();++save.index){save.cursor=0;auto& r=save.records[save.index];assert(Load(&save,r.type,r.version,static_cast<unsigned>(r.bytes.size())));}
 auto restored=bindings.at(1);assert(restored.position==pin.position&&restored.angle==pin.angle&&restored.releaseRequested&&restored.oldHands==4&&restored.flags==pin.flags);
 assert(tethers.at(9).phase==Phase::Reconciling&&!tethers.at(9).wanted);
 tethers[10]={Phase::Checking,true,20};SKSE::SerializationInterface filtered;Save(&filtered);assert(filtered.records[1].bytes.size()==12);
 bindings.clear();assert(HostileHook(&actor,&other)&&CombatHook(&actor,&other,nullptr));
 save.index=0;save.cursor=0;assert(Load(&save,'BIND',1,67)&&bindings.empty()); // malformed count/stride rejected
 actor.parent=&elsewhere;reenter=[&]{Freeze(&actor,pin);};auto before=moves;Freeze(&actor,pin);assert(moves==before+1&&actor.parent==&RE::cell);reenter={};
 actor.parent=&elsewhere;actor.pos=destination;failMove=true;before=moves;Freeze(&actor,pin);assert(moves==before+1&&actor.pos==destination);
 Freeze(&actor,pin,false);assert(moves==before+1); // NPC update cannot issue repeated cross-cell relocation
 bindings.clear();pin.releaseRequested=false;bindings[1]=pin;SKSE::SerializationInterface missingCell;Save(&missingCell);bindings.clear();missingCell.resolveCells=false;
 assert(Load(&missingCell,'BIND',1,68)&&bindings.at(1).releaseRequested&&bindings.at(1).cell==0); // retain restoration, do not strand restraint flags
 std::puts("PASS: actual pin hooks block cross-cell MoveTo while unloaded; clamp position; repair bypassed relocation; only D suppresses hostility/combat; BIND/TETH roundtrip and malformed record rejection. Not engine QA.");
}
`);
