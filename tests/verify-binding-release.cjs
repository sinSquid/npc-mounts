// Execute production Release/Loaded and hostility checks, not a replacement algorithm.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'src/Extras.cpp'),'utf8');
function take(sig){let start=source.indexOf(sig);assert(start>=0,sig);let end=source.indexOf('{',start)+1,n=1;while(n){const c=source[end++];if(c==='{')n++;if(c==='}')n--;}return source.slice(start,end);}
fs.writeFileSync(path.join(root,'build/binding-release-regression.cpp'),`
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <unordered_map>
namespace RE {
using FormID=unsigned;struct NiPoint3{};enum class ACTOR_LIFE_STATE {kAlive=0,kDead=2,kRestrained=6};enum class ITEM_REMOVE_REASON{kRemove};
struct TESBoundObject{};struct TESObjectARMO:TESBoundObject{};inline TESObjectARMO oldArmor,cuffArmor;
struct TESForm{template<class T>static T* LookupByID(unsigned id){return id==4?reinterpret_cast<T*>(&oldArmor):nullptr;}};
struct Actor {enum class BOOL_FLAGS:unsigned{kForceAnimGraphUpdate=1u<<29};unsigned id=1;bool dead=false,ai=true,hostile=true;int lifeWrites=0,idles=0,evaluated=0,counts=3;ACTOR_LIFE_STATE life=ACTOR_LIFE_STATE::kRestrained;
 struct Flags{unsigned value=0;void reset(BOOL_FLAGS v){value&=~unsigned(v);}void set(BOOL_FLAGS v){value|=unsigned(v);}};
 enum class BOOL_BITS:unsigned{kPlayerTeammate=1u<<26};
 struct Bits{unsigned value=0;void reset(BOOL_BITS v){value&=~unsigned(v);}void set(BOOL_BITS v){value|=unsigned(v);}};
 struct{Flags boolFlags;Bits boolBits;}data;
 bool IsPlayerTeammate(){return (data.boolBits.value&(1u<<26))!=0;}bool IsAIEnabled(){return ai;}
 unsigned GetFormID(){return id;}bool IsDead(){return dead;}Actor* AsActorState(){return this;}auto GetLifeState(){return life;}
 void SetLifeState(ACTOR_LIFE_STATE v){life=v;++lifeWrites;}auto& GetActorRuntimeData(){return data;}void EnableAI(bool v){ai=v;}
 void NotifyAnimationGraph(const char*){++idles;}void EvaluatePackage(bool,bool){++evaluated;}void* Get3D(){return this;}
 template<class F>auto GetInventoryCounts(F){return std::unordered_map<TESBoundObject*,int>{{&cuffArmor,counts}};}
 void RemoveItem(TESObjectARMO*,int n,ITEM_REMOVE_REASON,void*,void*){counts-=n;}
};
struct ActorEquipManager{TESObjectARMO* equipped=nullptr;unsigned unequips=0;static ActorEquipManager* GetSingleton(){static ActorEquipManager e;return &e;}
 template<class...T>void EquipObject(Actor*,TESObjectARMO* item,T...){equipped=item;}
 template<class...T>void UnequipObject(Actor*,TESObjectARMO*,T...){++unequips;}
};struct CombatGroup{};
}
namespace Mounts {bool Converted(RE::Actor*){return false;}}
constexpr auto flagsMask=static_cast<RE::Actor::BOOL_FLAGS>((1u<<27)|(1u<<20)|(1u<<21));
${take('struct Binding {')};${take('enum class Phase ')};${take('struct Tether {')};
std::mutex mutex;std::unordered_map<unsigned,Binding> bindings;std::unordered_map<unsigned,Tether> tethers;
auto* cuffs=&RE::cuffArmor;bool hooksReady=true;unsigned deleted=0,freeze=0;
void DeletePost(unsigned id){if(id)++deleted;}void StopTether(RE::Actor* a){tethers.at(a->id).wanted=false;}
void Freeze(RE::Actor*,const Binding&){++freeze;}
void StartTether(RE::Actor*,bool){}
bool originalHostile(RE::Actor* a,RE::Actor*){return a->hostile;}bool originalCombat(RE::Actor*,RE::Actor*,RE::CombatGroup*){return true;}
bool displaced=false,ready=true;bool Ready(RE::Actor*){return ready;}bool Displaced(RE::Actor*,const Binding&){return displaced;}
${take('bool Find(')}${take('bool Pacified(')}${take('bool HostileHook(')}${take('bool CombatHook(')}${take('bool Busy(')}${take('bool HoldAnchor(')}${take('bool Release(')}${take('void Loaded(')}
int main(){RE::Actor a,b;b.id=2;Binding binding;binding.pacified=true;binding.ai=false;binding.life=0;binding.flags=1u<<20;binding.priorCuffs=2;binding.oldHands=4;
 a.data.boolFlags.value=unsigned(flagsMask)|(1u<<18);bindings[1]=binding;
 assert(!HostileHook(&a,&b)&&!HostileHook(&b,&a)&&!CombatHook(&a,&b,nullptr));
 assert(Release(&a));assert(bindings.empty()&&a.counts==2&&!a.ai&&a.life==RE::ACTOR_LIFE_STATE::kAlive);
 assert(a.data.boolFlags.value==((1u<<18)|(1u<<20))&&RE::ActorEquipManager::GetSingleton()->equipped==&RE::oldArmor);
 assert(HostileHook(&a,&b)&&CombatHook(&a,&b,nullptr));a.hostile=false;assert(!HostileHook(&a,&b)); // restore dynamic relations, not force hostility
 auto writes=a.lifeWrites;auto unequips=RE::ActorEquipManager::GetSingleton()->unequips;Release(&a);assert(a.lifeWrites==writes&&a.counts==2&&RE::ActorEquipManager::GetSingleton()->unequips==unequips);
 binding.pacified=false;binding.post=3;binding.releaseRequested=true;bindings[1]=binding;Loaded(&a);assert(bindings.empty()&&deleted==1);
 a.dead=true;a.life=RE::ACTOR_LIFE_STATE::kDead;bindings[1]=binding;Loaded(&a);assert(bindings.empty()&&a.life==RE::ACTOR_LIFE_STATE::kDead);
 a.dead=false;binding.releaseRequested=false;binding.pacified=true;bindings[1]=binding;a.life=RE::ACTOR_LIFE_STATE::kAlive;Loaded(&a);assert(a.life==RE::ACTOR_LIFE_STATE::kRestrained&&a.ai&&freeze==1);Release(&a);
 for(bool teammate:{false,true}){binding.pacified=false;binding.ai=true;binding.teammate=teammate;binding.teammateSaved=true;bindings[1]=binding;a.ai=true;a.data.boolBits.value=(teammate?1u<<26:0)|(1u<<10);
  assert(HoldAnchor(&a)&&a.ai&&!a.IsPlayerTeammate());Release(&a);assert(a.ai&&a.IsPlayerTeammate()==teammate&&(a.data.boolBits.value&(1u<<10)));}
 binding.teammateSaved=false;bindings[1]=binding;a.data.boolBits.value=1u<<26;assert(HoldAnchor(&a));assert(bindings.at(1).teammateSaved&&bindings.at(1).teammate);Release(&a);assert(a.IsPlayerTeammate());
 bindings[1]=binding;displaced=true;a.ai=true;assert(HoldAnchor(&a)&&a.ai&&a.IsPlayerTeammate());auto before=freeze;Loaded(&a);assert(freeze==before);Release(&a);displaced=false;
 binding.life=6;bindings[1]=binding;a.life=RE::ACTOR_LIFE_STATE::kAlive;Release(&a);assert(a.life==RE::ACTOR_LIFE_STATE::kRestrained);
 bindings[1]=binding;ready=false;a.ai=true;auto idleBefore=a.idles;assert(HoldAnchor(&a)&&a.ai);Loaded(&a);assert(bindings.contains(1)&&a.ai&&a.idles==idleBefore);
 auto unequipBefore=RE::ActorEquipManager::GetSingleton()->unequips;assert(!Release(&a)&&bindings.at(1).releaseRequested&&a.ai&&RE::ActorEquipManager::GetSingleton()->unequips==unequipBefore);ready=true;Loaded(&a);assert(!bindings.contains(1));
 tethers[1]={Phase::Active,true,1};assert(!Release(&a)&&!tethers.at(1).wanted);
 tethers[1].phase=Phase::Failed;assert(Release(&a)&&tethers.contains(1)); // B unblocks local restoration without falsely claiming rope cleanup
 std::puts("PASS: actual C/D release, dynamic hostility restoration, original flags/AI/equipment, idempotence, deferred/dead load cleanup, late D load, B waits normally but not on terminal bridge failure.");
}
`);
