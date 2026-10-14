#include "pch.h"
#include "Extras.h"
#include "MinHook.h"
#include <array>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <atomic>
namespace Mounts {bool Converted(RE::Actor*);bool Riding(RE::Actor*);bool Restoring(RE::Actor*);}
namespace Extras {
namespace {
constexpr auto flagsMask=static_cast<RE::Actor::BOOL_FLAGS>((1u<<27)|(1u<<20)|(1u<<21));
struct Binding {
    RE::FormID post=0,cell=0,oldHands=0;
    std::uint32_t flags=0,life=0,priorCuffs=0;
    bool ai=true,pacified=false,releaseRequested=false;
    RE::NiPoint3 position,angle;
};
enum class Phase {Checking,Applying,Reconciling,Active,Stopping,Failed};
struct Tether {
    Phase phase=Phase::Checking;bool wanted=true;std::uint64_t token=0;
    float elapsed=0,retry=0;unsigned failures=0;
    bool pending=false,query=false;std::uint64_t born=0;
};
std::mutex mutex;
std::unordered_map<RE::FormID,Binding> bindings;
std::unordered_map<RE::FormID,Tether> tethers; // Main-thread tasks only.
std::uint64_t serial=0;
std::atomic_uint64_t session{0};
std::atomic_uint64_t leashEventsSeen{0};
float poll=0,anchorPoll=0;
bool hooksReady=false,leashAvailable=false;
RE::TESObjectSTAT* postBase{};RE::TESObjectARMO* cuffs{};
std::array<RE::EffectSetting*,4> effects{};
using Move=void(*)(RE::TESObjectREFR*,const RE::ObjectRefHandle&,RE::TESObjectCELL*,RE::TESWorldSpace*,const RE::NiPoint3&,const RE::NiPoint3&);
using Position=void(*)(RE::Actor*,const RE::NiPoint3&,bool);
using Hostile=bool(*)(RE::Actor*,RE::Actor*);
using Combat=bool(*)(RE::Actor*,RE::Actor*,RE::CombatGroup*);
Move originalMove{};Position originalPosition{};Hostile originalHostile{};Combat originalCombat{};

void Notify(const char* text){RE::SendHUDMessage::ShowHUDMessage(text);}
bool Find(RE::FormID id,Binding& out){std::scoped_lock lock(mutex);auto it=bindings.find(id);if(it==bindings.end())return false;out=it->second;return true;}
bool Pacified(RE::Actor* actor){Binding b;return actor&&Find(actor->GetFormID(),b)&&b.pacified;}
void MoveHook(RE::TESObjectREFR* actor,const RE::ObjectRefHandle& handle,RE::TESObjectCELL* cell,RE::TESWorldSpace* world,const RE::NiPoint3& pos,const RE::NiPoint3& angle){
    Binding b;if(actor&&Find(actor->GetFormID(),b)&&!b.pacified)return;
    originalMove(actor,handle,cell,world,pos,angle);
}
void PositionHook(RE::Actor* actor,const RE::NiPoint3& pos,bool update){
    Binding b;if(Find(actor->GetFormID(),b)&&!b.pacified){originalPosition(actor,b.position,update);return;}
    originalPosition(actor,pos,update);
}
bool HostileHook(RE::Actor* self,RE::Actor* target){return Pacified(self)||Pacified(target)?false:originalHostile(self,target);}
bool CombatHook(RE::Actor* self,RE::Actor* target,RE::CombatGroup* group){return Pacified(self)||Pacified(target)?false:originalCombat(self,target,group);}
void Freeze(RE::Actor* actor,const Binding& b,bool repairCell=true){
    if(b.pacified)return;
    // MoveTo can synchronously update the actor. Do not re-enter relocation.
    static thread_local bool updating=false;if(updating)return;
    struct Guard{bool& value;Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(updating);
    if(auto* cell=RE::TESForm::LookupByID<RE::TESObjectCELL>(b.cell);cell&&actor->GetParentCell()!=cell){
        if(!repairCell)return;
        RE::ObjectRefHandle empty;originalMove(actor,empty,cell,cell->GetRuntimeData().worldSpace,b.position,b.angle);
        if(actor->GetParentCell()!=cell)return;
    }
    if(actor->Get3D()){
        if(actor->GetPosition()!=b.position)originalPosition(actor,b.position,true);
        if(actor->GetAngle()!=b.angle)actor->SetAngle(b.angle);
        if(auto* cc=actor->GetCharController())cc->SetLinearVelocityImpl(RE::hkVector4());
    }
}
bool Eligible(RE::Actor* actor){
    return actor&&!actor->IsPlayerRef()&&!actor->IsDead()&&!Mounts::Restoring(actor)&&actor->GetParentCell()&&actor->Get3D()&&actor->Get3D()->GetObjectByName("NPC Spine2 [Spn2]")&&
        !actor->GetCurrentScene()&&!actor->IsInKillMove()&&!actor->GetOccupiedFurniture()&&!actor->IsOnMount();
}
void DeletePost(RE::FormID id){if(auto* ref=RE::TESForm::LookupByID<RE::TESObjectREFR>(id)){ref->Disable();ref->SetDelete(true);}}
bool Bind(RE::Actor* actor,bool pacify){
    if(!hooksReady||!postBase||!cuffs){Notify("束缚功能未初始化，请查看NPCMounts日志");return false;}
    if(!Eligible(actor)||Bound(actor)||Busy(actor)||Mounts::Riding(actor)){Notify("目标状态不允许束缚；请先下骑并解除牵引");return false;}
    if(pacify ? !actor->IsHostileToActor(RE::PlayerCharacter::GetSingleton()) : !Mounts::Converted(actor)){
        Notify(pacify?"D只对敌对人形NPC有效":"C只对已转换的坐骑NPC有效");return false;
    }
    const auto life=actor->AsActorState()->GetLifeState();
    if(life!=RE::ACTOR_LIFE_STATE::kAlive&&life!=RE::ACTOR_LIFE_STATE::kRestrained)return false;
    Binding b;b.cell=actor->GetParentCell()->GetFormID();b.position=actor->GetPosition();b.angle=actor->GetAngle();b.pacified=pacify;
    b.flags=actor->GetActorRuntimeData().boolFlags.underlying()&static_cast<std::uint32_t>(flagsMask);b.life=static_cast<unsigned>(life);b.ai=actor->IsAIEnabled();
    if(auto* old=actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kHands))b.oldHands=old->GetFormID();
    const auto counts=actor->GetInventoryCounts([&](RE::TESBoundObject& object){return &object==cuffs;});
    if(auto it=counts.find(cuffs);it!=counts.end())b.priorCuffs=std::max(0,it->second);
    if(!pacify){
        auto prop=actor->PlaceObjectAtMe(postBase,true);if(!prop){Notify("固定桩生成失败，未改变目标");return false;}
        b.post=prop->GetFormID();const float yaw=b.angle.z;
        prop->SetPosition(b.position+RE::NiPoint3{std::cos(yaw)*80.f,-std::sin(yaw)*80.f,0});prop->SetAngle(b.angle);
    }
    {std::scoped_lock lock(mutex);bindings.emplace(actor->GetFormID(),b);}
    actor->AddObjectToContainer(cuffs,nullptr,1,nullptr);
    RE::ActorEquipManager::GetSingleton()->EquipObject(actor,cuffs,nullptr,1,nullptr,false,true,false,true);
    actor->StopCombat();actor->EnableAI(true);actor->SetLifeState(RE::ACTOR_LIFE_STATE::kRestrained);actor->GetActorRuntimeData().boolFlags.set(flagsMask);
    actor->NotifyAnimationGraph("SprintStop");actor->NotifyAnimationGraph("moveStop");
    if(pacify&&!Mounts::Converted(actor))actor->NotifyAnimationGraph("IdleBoundStandingStart");
    Notify(pacify?"目标已束缚并停止敌对；B可解除":"坐骑已固定在此处；B可解除");return true;
}

struct Result final:RE::BSScript::IStackCallbackFunctor {
    std::function<void(bool)> done;
    bool holder;
    explicit Result(std::function<void(bool)> fn,bool owner):done(std::move(fn)),holder(owner){}
    void operator()(RE::BSScript::Variable result)override{
        // A missing/invalid Boolean result is not a successful disconnect.
        // Leave the request pending for the bounded watchdog instead.
        if(holder ? (!result.IsObject()&&!result.IsNoneObject()) : !result.IsBool())return;
        auto fn=done;const bool value=holder ? (result.IsObject()&&result.Unpack<RE::Actor*>()==RE::PlayerCharacter::GetSingleton()) : result.IsBool()&&result.GetBool();
        SKSE::GetTaskInterface()->AddTask([fn=std::move(fn),value]{fn(value);});
    }
    void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&)override{}
};
template<class... Args> bool Call(const char* function,std::function<void(bool)> done,Args... args){
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result{new Result(std::move(done),std::string_view(function)=="GetLeashHolder")};
    return RE::BSScript::Internal::VirtualMachine::GetSingleton()->DispatchStaticCall("LeashFramework",function,RE::MakeFunctionArguments(std::move(args)...),result);
}
bool Current(RE::FormID id,std::uint64_t token){const auto it=tethers.find(id);return it!=tethers.end()&&it->second.token==token;}
void FailTether(Tether& tether){
    if(tether.phase==Phase::Failed)return;
    tether.phase=Phase::Failed;tether.wanted=false;
    Notify("牵引接口未完成，已停止自动重试；B可还原本地状态，绳索清理尚未确认");
}
void StopTether(RE::Actor* actor){
    if(!actor)return;const auto id=actor->GetFormID();auto it=tethers.find(id);if(it==tethers.end())return;
    it->second.wanted=false;if(it->second.phase!=Phase::Active||it->second.retry>0)return;
    if(!leashAvailable){tethers.erase(it);return;}
    it->second.phase=Phase::Stopping;it->second.pending=true;it->second.elapsed=0;const auto token=it->second.token;
    if(!Call("DisconnectLeash",[id,token](bool){if(Current(id,token))tethers.erase(id);},RE::PlayerCharacter::GetSingleton()->As<RE::Actor>(),actor)){
        it->second.pending=false;it->second.phase=Phase::Active;it->second.retry=1.f;
        if(++it->second.failures>=3)FailTether(it->second);
    }
}
void StartTether(RE::Actor* actor){
    if(!leashAvailable){Notify("E需要启用LeashFramework 1.1.7及Leash.esm");return;}
    if(!Eligible(actor)||!Mounts::Converted(actor)||Bound(actor)||Busy(actor)||Mounts::Riding(actor)){
        Notify("E只对未固定、未骑乘、未牵引的坐骑NPC有效");return;
    }
    const auto id=actor->GetFormID();const auto handle=actor->GetHandle();const auto token=++serial;
    tethers.emplace(id,Tether{Phase::Checking,true,token});tethers.at(id).born=leashEventsSeen.load();tethers.at(id).pending=true;
    if(!Call("IsLeashed",[id,handle,token](bool occupied){
        if(!Current(id,token))return;auto ref=handle.get();
        if(occupied||!ref||!tethers.at(id).wanted||!Eligible(ref.get())||!Mounts::Converted(ref.get())){tethers.erase(id);if(occupied)Notify("目标已有其他牵引，未覆盖");return;}
        tethers.at(id).phase=Phase::Applying;tethers.at(id).elapsed=0;
        if(!Call("ApplyStandaloneLeash",[id,handle,token](bool applied){
            if(!Current(id,token))return;
            if(!applied){tethers.erase(id);Notify("LeashFramework未能创建牵引绳，请查看日志");return;}
            auto ref=handle.get();tethers.at(id).phase=Phase::Active;tethers.at(id).pending=false;tethers.at(id).elapsed=0;
            if(!ref){tethers.erase(id);return;}
            if(!tethers.at(id).wanted||!Eligible(ref.get())||!Mounts::Converted(ref.get())){StopTether(ref.get());return;}
            // Keep humanoid crawl locomotion: no ragdoll pull.
            Call("SetRagdollOverride",[](bool){},ref.get(),0);
            Notify("已连接牵引绳；F解除，B完整还原");
        },RE::PlayerCharacter::GetSingleton()->As<RE::Actor>(),ref.get(),std::string("Leash/neck_rope.nif"),std::string("NPC Spine2 [Spn2]"),std::string("Leash1_0"),std::string("Leash1_1_"),100.f,220.f,true,true,0.f,0.f,8.f)){
            tethers.erase(id);Notify("牵引接口调用失败，未进入牵引状态");
        }
    },actor)){tethers.erase(id);Notify("无法访问LeashFramework脚本接口");}
}
void Unleashed(RE::FormID id,std::uint64_t epoch,std::uint64_t sequence){
    if(session.load()!=epoch)return;auto it=tethers.find(id);
    if(it!=tethers.end()&&it->second.born<sequence&&it->second.phase!=Phase::Applying&&it->second.phase!=Phase::Checking&&!it->second.pending)tethers.erase(it);
}
struct LeashEvents final:RE::BSTEventSink<SKSE::ModCallbackEvent>{
    RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,RE::BSTEventSource<SKSE::ModCallbackEvent>*)override{
        if(event&&event->sender&&event->eventName=="LeashFramework_OnUnleash"){
            const auto id=event->sender->GetFormID();const auto epoch=session.load();const auto sequence=++leashEventsSeen;
            // Replacement/disconnect events mean we no longer own that rope.
            SKSE::GetTaskInterface()->AddTask([id,epoch,sequence]{Unleashed(id,epoch,sequence);});
        }return RE::BSEventNotifyControl::kContinue;
    }
} leashEvents;
}
bool Bound(RE::Actor* actor){Binding b;return actor&&Find(actor->GetFormID(),b);}
bool Leashed(RE::Actor* actor){if(!actor)return false;auto it=tethers.find(actor->GetFormID());return it!=tethers.end()&&it->second.phase==Phase::Active&&it->second.wanted;}
bool Busy(RE::Actor* actor){return actor&&tethers.contains(actor->GetFormID());}
bool Release(RE::Actor* actor){
    if(!actor)return true;if(Busy(actor)){StopTether(actor);if(Busy(actor)&&tethers.at(actor->GetFormID()).phase!=Phase::Failed)return false;}
    Binding b;const auto id=actor->GetFormID();if(!Find(id,b))return true;
    {std::scoped_lock lock(mutex);bindings.erase(id);}
    if(cuffs){
        auto* equip=RE::ActorEquipManager::GetSingleton();equip->UnequipObject(actor,cuffs,nullptr,1,nullptr,false,true,false,true);
        const auto counts=actor->GetInventoryCounts([&](RE::TESBoundObject& object){return &object==cuffs;});
        if(auto it=counts.find(cuffs);it!=counts.end()&&it->second>static_cast<int>(b.priorCuffs))actor->RemoveItem(cuffs,1,RE::ITEM_REMOVE_REASON::kRemove,nullptr,nullptr);
        if(auto* old=RE::TESForm::LookupByID<RE::TESObjectARMO>(b.oldHands))equip->EquipObject(actor,old,nullptr,1,nullptr,false,false,false,true);
    }
    DeletePost(b.post);auto& flags=actor->GetActorRuntimeData().boolFlags;flags.reset(flagsMask);flags.set(static_cast<RE::Actor::BOOL_FLAGS>(b.flags));
    if(!actor->IsDead()&&actor->AsActorState()->GetLifeState()==RE::ACTOR_LIFE_STATE::kRestrained)actor->SetLifeState(static_cast<RE::ACTOR_LIFE_STATE>(b.life));
    actor->EnableAI(b.ai);
    if(!actor->IsDead()){actor->NotifyAnimationGraph("IdleForceDefaultState");if(b.ai)actor->EvaluatePackage(false,false);}return true;
}
void ReleaseAll(){
    std::vector<RE::FormID> ids;{std::scoped_lock lock(mutex);for(auto& [id,b]:bindings){b.releaseRequested=true;ids.push_back(id);}}
    for(auto& [id,t]:tethers){t.wanted=false;if(std::find(ids.begin(),ids.end(),id)==ids.end())ids.push_back(id);}
    for(auto id:ids)if(auto* actor=RE::TESForm::LookupByID<RE::Actor>(id))Release(actor);
}
void AfterActorUpdate(RE::Actor* actor){Binding b;if(hooksReady&&Find(actor->GetFormID(),b)&&!actor->IsDead())Freeze(actor,b,false);}
void CheckTether(RE::Actor* actor){
    const auto id=actor->GetFormID();auto& tether=tethers.at(id);
    if(tether.phase!=Phase::Active&&tether.phase!=Phase::Reconciling)return;
    if(tether.query||tether.pending||tether.retry>0)return;
    tether.query=true;tether.elapsed=0;const auto token=tether.token;
    if(!Call("GetLeashHolder",[id,token](bool owned){
        if(!Current(id,token))return;auto& t=tethers.at(id);t.query=false;
        if(t.phase!=Phase::Active&&t.phase!=Phase::Reconciling&&t.phase!=Phase::Failed)return;
        if(t.pending)return;
        if(!owned){tethers.erase(id);return;}
        t.phase=Phase::Active;t.elapsed=0;t.failures=0;
    },actor)){tether.query=false;tether.retry=1.f;if(++tether.failures>=3)FailTether(tether);}
}
void AdvanceTethers(float delta){
    for(auto it=tethers.begin();it!=tethers.end();){auto& t=it->second;t.retry=std::max(0.f,t.retry-delta);
        if(t.pending||t.query){t.elapsed+=delta;if(t.elapsed>=10.f){
            if(t.phase==Phase::Checking){it=tethers.erase(it);Notify("牵引检查超时，未创建绳索");continue;}
            if(t.query&&!t.pending){t.query=false;t.token=++serial;} // Read-only timeout: safe to invalidate the late reply.
            FailTether(t); // Keep ownership and any in-flight call; never duplicate a mutating request.
        }}++it;
    }
}
void Tick(float delta){
    AdvanceTethers(delta);anchorPoll-=delta;const bool repairCell=anchorPoll<=0;if(repairCell)anchorPoll=1.f;
    std::vector<std::pair<RE::FormID,Binding>> work;{std::scoped_lock lock(mutex);for(const auto& item:bindings)work.push_back(item);}
    for(const auto& [id,b]:work)if(auto* actor=RE::TESForm::LookupByID<RE::Actor>(id)){
        if(actor->IsDead()||b.releaseRequested){Release(actor);continue;}
        actor->GetActorRuntimeData().boolFlags.set(flagsMask);
        if(actor->AsActorState()->GetLifeState()==RE::ACTOR_LIFE_STATE::kAlive)actor->SetLifeState(RE::ACTOR_LIFE_STATE::kRestrained);
        Freeze(actor,b,repairCell);
        if(b.pacified&&actor->IsInCombat())actor->StopCombat();
    }
    poll-=delta;if(poll>0)return;poll=1.f;
    std::vector<RE::FormID> ids;for(const auto& [id,t]:tethers)ids.push_back(id);
    for(auto id:ids){auto it=tethers.find(id);if(it==tethers.end())continue;auto* actor=RE::TESForm::LookupByID<RE::Actor>(id);if(!actor)continue;
        if(it->second.phase==Phase::Failed)continue;
        if(it->second.phase==Phase::Reconciling){if(Bound(actor)||!Mounts::Converted(actor))it->second.wanted=false;CheckTether(actor);continue;}
        if(!it->second.wanted||actor->IsDead()||!Mounts::Converted(actor)){StopTether(actor);continue;}
        if(it->second.phase==Phase::Active)CheckTether(actor);
    }
}
bool Handle(RE::FormID effect,RE::Actor* actor){
    for(unsigned i=0;i<effects.size();++i)if(effects[i]&&effects[i]->GetFormID()==effect){
        if(i==0)Bind(actor,false);else if(i==1)Bind(actor,true);else if(i==2)StartTether(actor);else if(Busy(actor)){
            auto& t=tethers.at(actor->GetFormID());
            if(t.phase==Phase::Failed&&!t.pending&&!t.query){t.phase=Phase::Reconciling;t.failures=0;t.retry=0;}
            StopTether(actor);
        }else Notify("目标没有本Mod创建的牵引");return true;
    }return false;
}
bool Matches(RE::FormID effect){return std::any_of(effects.begin(),effects.end(),[effect](auto* item){return item&&item->GetFormID()==effect;});}
void Initialize(){
    auto* data=RE::TESDataHandler::GetSingleton();postBase=data->LookupForm<RE::TESObjectSTAT>(0x820,"NPCMounts.esp");cuffs=data->LookupForm<RE::TESObjectARMO>(0x821,"NPCMounts.esp");
    for(unsigned i=0;i<4;++i)effects[i]=data->LookupForm<RE::EffectSetting>(0x806+i,"NPCMounts.esp");
    leashAvailable=GetModuleHandleW(L"LeashFramework.dll")&&data->LookupModByName("Leash.esm");
    if(leashAvailable)SKSE::GetModCallbackEventSource()->AddEventSink(&leashEvents);
    const auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED){spdlog::error("Restraint hook init failed {}",int(status));return;}
    struct Hook{void* address;void* replacement;void** original;};
    const std::array hooks{
        Hook{reinterpret_cast<void*>(REL::RelocationID(56227,56626).address()),reinterpret_cast<void*>(MoveHook),reinterpret_cast<void**>(&originalMove)},
        Hook{reinterpret_cast<void*>(REL::RelocationID(36537,37537).address()),reinterpret_cast<void*>(HostileHook),reinterpret_cast<void**>(&originalHostile)},
        Hook{reinterpret_cast<void*>(REL::RelocationID(37608,38561).address()),reinterpret_cast<void*>(CombatHook),reinterpret_cast<void**>(&originalCombat)}};
    for(const auto& h:hooks){const auto result=MH_CreateHook(h.address,h.replacement,h.original);if(result!=MH_OK){spdlog::error("Restraint detour failed {}",int(result));return;}MH_QueueEnableHook(h.address);}
    if(MH_ApplyQueued()!=MH_OK){spdlog::error("Restraint detour activation failed");return;}
    REL::Relocation<std::uintptr_t> npc{RE::VTABLE_Character[0]};originalPosition=reinterpret_cast<Position>(npc.write_vfunc(0xA9,PositionHook));hooksReady=true;
    spdlog::info("C/D restraint hooks ready; E/F LeashFramework available={}",leashAvailable);
}
void Save(SKSE::SerializationInterface* save){
    {std::scoped_lock lock(mutex);save->OpenRecord('BIND',1);save->WriteRecordData(static_cast<std::uint32_t>(bindings.size()));
    for(const auto& [id,b]:bindings){for(auto v:{id,b.post,b.cell,b.flags,b.life,std::uint32_t(b.ai),b.oldHands,b.priorCuffs,std::uint32_t(b.pacified),std::uint32_t(b.releaseRequested)})save->WriteRecordData(v);
        for(float v:{b.position.x,b.position.y,b.position.z,b.angle.x,b.angle.y,b.angle.z})save->WriteRecordData(v);}}
    // A preflight check has not created anything: never claim an unrelated rope on load.
    save->OpenRecord('TETH',1);save->WriteRecordData(static_cast<std::uint32_t>(std::count_if(tethers.begin(),tethers.end(),[](const auto& pair){return pair.second.phase!=Phase::Checking;})));
    for(const auto& [id,t]:tethers)if(t.phase!=Phase::Checking){save->WriteRecordData(id);save->WriteRecordData(std::uint32_t(t.wanted));}
}
bool Load(SKSE::SerializationInterface* save,std::uint32_t type,std::uint32_t version,std::uint32_t length){
    if(type!='BIND'&&type!='TETH')return false;if(version!=1||length<4)return true;
    const auto stride=type=='BIND'?64u:8u;std::uint32_t count=0;if(save->ReadRecordData(&count,4)!=4||length!=4+std::uint64_t(count)*stride)return true;
    for(unsigned i=0;i<count;++i){
        if(type=='TETH'){std::uint32_t id=0,wanted=0,resolved=0;if(save->ReadRecordData(&id,4)!=4||save->ReadRecordData(&wanted,4)!=4)break;if(save->ResolveFormID(id,resolved)){tethers[resolved]={Phase::Reconciling,wanted!=0,++serial};tethers[resolved].born=leashEventsSeen.load();}continue;}
        std::array<std::uint32_t,10> values;std::array<float,6> coordinates;if(save->ReadRecordData(values.data(),40)!=40||save->ReadRecordData(coordinates.data(),24)!=24)break;
        if(!std::all_of(coordinates.begin(),coordinates.end(),[](float n){return std::isfinite(n);})||(values[4]!=0&&values[4]!=6))continue;
        RE::FormID id;if(!save->ResolveFormID(values[0],id))continue;
        Binding b;save->ResolveFormID(values[1],b.post);save->ResolveFormID(values[2],b.cell);save->ResolveFormID(values[6],b.oldHands);
        b.flags=values[3]&static_cast<unsigned>(flagsMask);b.life=values[4];b.ai=values[5]!=0;b.priorCuffs=values[7];b.pacified=values[8]!=0;b.releaseRequested=values[9]!=0||(!b.cell&&!b.pacified);
        b.position={coordinates[0],coordinates[1],coordinates[2]};b.angle={coordinates[3],coordinates[4],coordinates[5]};std::scoped_lock lock(mutex);bindings[id]=b;
    }return true;
}
void Revert(){std::scoped_lock lock(mutex);++session;bindings.clear();tethers.clear();++serial;poll=anchorPoll=0;}
std::uint64_t Session(){return session.load();}
void Deleted(RE::FormID id){Binding b;if(Find(id,b)){{std::scoped_lock lock(mutex);bindings.erase(id);}DeletePost(b.post);}tethers.erase(id);}
void Loaded(RE::Actor* actor){
    Binding b;if(!actor||!Find(actor->GetFormID(),b))return;
    if(!hooksReady||actor->IsDead()||b.releaseRequested){Release(actor);return;}
    actor->EnableAI(true);actor->SetLifeState(RE::ACTOR_LIFE_STATE::kRestrained);actor->GetActorRuntimeData().boolFlags.set(flagsMask);Freeze(actor,b);
    if(b.pacified&&!Mounts::Converted(actor)&&actor->Get3D())actor->NotifyAnimationGraph("IdleBoundStandingStart");
}
void PostLoad(){
    if(!leashAvailable)tethers.clear();
    if(!hooksReady){ReleaseAll();return;}
    std::vector<std::pair<RE::FormID,Binding>> work;{std::scoped_lock lock(mutex);for(const auto& entry:bindings)work.push_back(entry);}
    for(const auto& [id,b]:work)Loaded(RE::TESForm::LookupByID<RE::Actor>(id));
}
}
