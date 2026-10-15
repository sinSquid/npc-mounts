const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),s=fs.readFileSync(path.join(root,'src/Extras.cpp'),'utf8');
function take(sig){let start=s.indexOf(sig);assert(start>=0);let end=s.indexOf('{',start)+1,n=1;while(n){let c=s[end++];if(c==='{')n++;if(c==='}')n--;}return s.slice(start,end);}
fs.writeFileSync(path.join(root,'build/capture-regression.cpp'),`
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstdint>
namespace RE{
using FormID=unsigned;struct NiPoint3{float x{},y{},z{};NiPoint3 operator+(NiPoint3 b){return{x+b.x,y+b.y,z+b.z};}};enum class ACTOR_LIFE_STATE{kAlive,kDead,kRestrained};struct TESBoundObject{unsigned GetFormID(){return 3;}};struct BGSBipedObjectForm{enum class BipedObjectSlot{kHands};};struct Prop{unsigned GetFormID(){return 99;}void SetPosition(NiPoint3){}void SetAngle(NiPoint3){}};
struct Actor{unsigned id=1;bool hostile=true,converted=false,convertOK=true,postOK=true;int converts=0,posts=0;ACTOR_LIFE_STATE life=ACTOR_LIFE_STATE::kAlive;struct Flags{unsigned value=8;unsigned underlying(){return value;}void set(unsigned v){value|=v;}};struct{Flags boolFlags;}data;
 unsigned GetFormID(){return id;}bool IsHostileToActor(Actor*){return hostile;}Actor* AsActorState(){return this;}auto GetLifeState(){return life;}std::shared_ptr<Prop> PlaceObjectAtMe(TESBoundObject*,bool){++posts;return postOK?std::make_shared<Prop>():nullptr;}Actor* GetParentCell(){return this;}NiPoint3 GetPosition(){return{};}NiPoint3 GetAngle(){return{};}auto& GetActorRuntimeData(){return data;}bool IsAIEnabled(){return true;}bool IsPlayerTeammate(){return true;}TESBoundObject* GetWornArmor(BGSBipedObjectForm::BipedObjectSlot){return nullptr;}template<class F>auto GetInventoryCounts(F){return std::unordered_map<TESBoundObject*,int>{};}void AddObjectToContainer(TESBoundObject*,void*,int,void*){}void StopCombat(){}void EnableAI(bool){}void SetLifeState(ACTOR_LIFE_STATE v){life=v;}void NotifyAnimationGraph(const char*){}
 };struct PlayerCharacter{static Actor* GetSingleton(){static Actor a;return &a;}};struct ActorEquipManager{static ActorEquipManager* GetSingleton(){static ActorEquipManager x;return &x;}template<class...T>void EquipObject(T...){}};
}
namespace Mounts{bool Converted(RE::Actor* a){return a->converted;}bool Riding(RE::Actor*){return false;}bool Convert(RE::Actor* a){++a->converts;if(!a->convertOK)return false;a->converted=true;a->life=RE::ACTOR_LIFE_STATE::kRestrained;a->data.boolFlags.value=16;return true;}}
namespace spdlog{template<class...T>void info(const char*,T...){}}
${take('struct Binding {')};std::mutex mutex;std::unordered_map<unsigned,Binding> bindings;struct Tether{bool anchorAfterDisconnect=false;};std::unordered_map<unsigned,Tether>tethers;
bool leashAvailable=true,hooksReady=true;RE::TESBoundObject post,cuff;auto* postBase=&post;auto* cuffs=&cuff;unsigned flagsMask=7;int deleted=0,anchored=0,tethered=0;
void Notify(const char*){}bool Eligible(RE::Actor* a){return a;}bool Leashed(RE::Actor*){return false;}bool Bound(RE::Actor* a){return a&&bindings.contains(a->id);}bool Busy(RE::Actor*){return false;}void StopTether(RE::Actor*){}void DeletePost(unsigned){++deleted;}void HoldAnchor(RE::Actor*){++anchored;}void StartTether(RE::Actor*,bool anchor){assert(anchor);++tethered;}
${take('bool Bind(')}
int main(){RE::Actor a;a.hostile=false;assert(!Bind(&a,true)&&a.posts==0&&!a.converted);a.hostile=true;leashAvailable=false;assert(!Bind(&a,true)&&a.posts==0);leashAvailable=true;a.postOK=false;assert(!Bind(&a,true)&&a.converts==0);a.postOK=true;a.convertOK=false;assert(!Bind(&a,true)&&deleted==1&&bindings.empty());a.convertOK=true;assert(Bind(&a,true)&&a.converted&&bindings.at(1).pacified&&bindings.at(1).post==99&&anchored==1&&tethered==1);assert(bindings.at(1).life==unsigned(RE::ACTOR_LIFE_STATE::kRestrained));assert(!Bind(&a,true));bindings.clear();int before=a.converts;assert(Bind(&a,true)&&a.converts==before);bindings.clear();a.converted=false;assert(!Bind(&a,false)&&!a.converted);a.converted=true;assert(Bind(&a,false)&&!bindings.at(1).pacified&&bindings.at(1).post==99);std::puts("PASS: production hostile capture = convert + anchor; hostile-only, dependency/prop/conversion failures, repeated and already-converted targets, C boundary and conversion-state restoration ledger.");}
`);
