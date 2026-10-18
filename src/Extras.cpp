#include "pch.h"
#include "Extras.h"
#include "Wardrobe.h"
#include "Crawl.h"
#include "Diagnostics.h"
#include "MinHook.h"
#include <array>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <atomic>
namespace Mounts {bool Converted(RE::Actor*);bool Riding(RE::Actor*);bool Restoring(RE::Actor*);void Restore(RE::Actor*);}
namespace Extras {
namespace {
constexpr auto flagsMask=static_cast<RE::Actor::BOOL_FLAGS>((1u<<27)|(1u<<20)|(1u<<21));
struct Binding {
    RE::FormID post=0,cell=0,oldHands=0;
    std::uint32_t flags=0,life=0,priorCuffs=0;
    bool ai=true,pacified=false,releaseRequested=false;
    bool teammate=false,teammateSaved=false;
    RE::NiPoint3 position,angle;
};
enum class Phase {Checking,Applying,Reconciling,Active,Stopping,Failed};
struct Tether {
    Phase phase=Phase::Checking;bool wanted=true;std::uint64_t token=0;
    float elapsed=0,retry=0;unsigned failures=0;
    bool pending=false,query=false;std::uint64_t born=0;
    bool anchor=false;
    bool anchorAfterDisconnect=false;
};
std::mutex mutex;
std::unordered_map<RE::FormID,Binding> bindings;
std::unordered_map<RE::FormID,Tether> tethers; // Main-thread tasks only.
std::uint64_t serial=0;
std::atomic_uint64_t session{0};
std::atomic_uint64_t leashEventsSeen{0};
float poll=0;
bool hooksReady=false,leashAvailable=false;
void StartTether (RE::Actor* actor,bool anchor=false);
void StopTether (RE::Actor* actor);
RE::TESObjectSTAT* postBase{};RE::TESObjectARMO* cuffs{};
std::array<RE::EffectSetting*,4> effects{};
using Hostile=bool(*)(RE::Actor*,RE::Actor*);
using Combat=bool(*)(RE::Actor*,RE::Actor*,RE::CombatGroup*);
Hostile originalHostile{};Combat originalCombat{};

void Notify(const char* text){Diagnostics::Trace("Notice: {}",text);RE::SendHUDMessage::ShowHUDMessage(text);}
bool Find(RE::FormID id,Binding& out){std::scoped_lock lock(mutex);auto it=bindings.find(id);if(it==bindings.end())return false;out=it->second;return true;}
bool Pacified(RE::Actor* actor){Binding b;return actor&&Find(actor->GetFormID(),b)&&b.pacified;}
bool Ready(RE::Actor* actor){
    auto* cell=actor->GetParentCell();
    return cell&&cell->IsAttached()&&actor->Get3D();
}
bool SameSpace(RE::TESObjectCELL* first,RE::TESObjectCELL* second){
    if(!first||!second)return false;
    if(first==second)return true;
    auto* world=first->GetRuntimeData().worldSpace;
    return world&&world==second->GetRuntimeData().worldSpace;
}
bool Displaced(RE::Actor* actor,const Binding& b){
    auto* cell=actor->GetParentCell();
    if(!cell)return true;
    if(cell->GetFormID()!=b.cell){
        auto* oldCell=RE::TESForm::LookupByID<RE::TESObjectCELL>(b.cell);
        if(!SameSpace(cell,oldCell))return true;
    }
    const auto delta=actor->GetPosition()-b.position;
    return delta.x*delta.x+delta.y*delta.y+delta.z*delta.z>32.f*32.f;
}
bool HostileHook(RE::Actor* self,RE::Actor* target){return Pacified(self)||Pacified(target)?false:originalHostile(self,target);}
bool CombatHook(RE::Actor* self,RE::Actor* target,RE::CombatGroup* group){return Pacified(self)||Pacified(target)?false:originalCombat(self,target,group);}
void Freeze(RE::Actor* actor,const Binding& b){
    if(!b.post||b.releaseRequested||Displaced(actor,b))return;
    // Never relocate across cells or rewrite a loader's SetPosition call.
    // A queued character may still hold the old cell's loading state.
    auto* cell=actor->GetParentCell();
    if(!cell||!cell->IsAttached()||!actor->Get3D())return;
    if(auto* cc=actor->GetCharController())cc->SetLinearVelocityImpl(RE::hkVector4());
}
bool Eligible(RE::Actor* actor,bool humanoid=true){
    RE::NiPointer<RE::Actor> nativeMount;
    return actor&&!actor->IsPlayerRef()&&!actor->IsDead()&&!Mounts::Restoring(actor)&&actor->GetParentCell()&&actor->Get3D()&&(!humanoid||actor->Get3D()->GetObjectByName("NPC Spine2 [Spn2]"))&&
        !actor->IsInKillMove()&&actor->AsActorState()->GetSitSleepState()==RE::SIT_SLEEP_STATE::kNormal&&!actor->GetMount(nativeMount);
}
void DeletePost(RE::FormID id){if(auto* ref=RE::TESForm::LookupByID<RE::TESObjectREFR>(id)){ref->Disable();ref->SetDelete(true);}}
bool Bind(RE::Actor* actor,bool pacify){
    Diagnostics::Snapshot(actor,pacify?"Capture requested":"Anchor requested");
    Diagnostics::Trace("Bind converted={} bound={} busy={} ridden={}",Mounts::Converted(actor),Bound(actor),Busy(actor),Mounts::Riding(actor));
    if(!actor){Notify("未选择有效NPC目标");return false;}
    Binding existing;if(Find(actor->GetFormID(),existing)){
        if(existing.pacified!=pacify){Notify(pacify?"目标已处于固定状态，请先解除固定":"目标已处于捕获状态，请使用人魅-捕获或统一解除");return false;}
        Release(actor);Notify(pacify?"已解除捕获并恢复原状态":"已解除固定");return true;
    }
    const bool converted=Mounts::Converted(actor);
    if(!pacify&&!converted){Notify("人魅-固定NPC只对已转换的坐骑NPC有效");return false;}
    if(!pacify&&!leashAvailable){Notify("人魅-固定NPC需要启用LeashFramework及Leash.esm");return false;}
    if(!hooksReady||!cuffs||(!pacify&&!postBase)){Notify("束缚功能未初始化，请查看NPCMounts日志");return false;}
    if(!pacify&&Eligible(actor)&&Leashed(actor)&&!Bound(actor)&&!Mounts::Riding(actor)){
        tethers.at(actor->GetFormID()).anchorAfterDisconnect=true;StopTether(actor);
        Notify("正在把玩家牵引绳改接到固定桩");return true;
    }
    if(!Eligible(actor,!pacify)||Busy(actor)||Mounts::Riding(actor)){
        Diagnostics::Snapshot(actor,"Bind rejected");
        Notify("目标状态不允许束缚；请先下骑并解除牵引");return false;
    }
    const auto life=actor->AsActorState()->GetLifeState();
    if(life!=RE::ACTOR_LIFE_STATE::kAlive&&life!=RE::ACTOR_LIFE_STATE::kRestrained){Diagnostics::Trace("Bind rejected life={}",static_cast<unsigned>(life));return false;}
    if(converted){Mounts::Restore(actor);if(Mounts::Converted(actor)){Notify("坐骑仍在还原，稍后重试固定或捕获");return false;}}
    auto prop=pacify?RE::NiPointer<RE::TESObjectREFR>{}:actor->PlaceObjectAtMe(postBase,true);if(!pacify&&!prop){Notify("固定桩生成失败，未改变目标");return false;}
    Binding b;b.cell=actor->GetParentCell()->GetFormID();b.position=actor->GetPosition();b.angle=actor->GetAngle();b.pacified=pacify;
    b.flags=actor->GetActorRuntimeData().boolFlags.underlying()&static_cast<std::uint32_t>(flagsMask);b.life=static_cast<unsigned>(actor->AsActorState()->GetLifeState());b.ai=actor->IsAIEnabled();
    b.teammate=actor->IsPlayerTeammate();b.teammateSaved=true;
    if(auto* old=actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kHands))b.oldHands=old->GetFormID();
    const auto counts=actor->GetInventoryCounts([&](RE::TESBoundObject& object){return &object==cuffs;});
    if(auto it=counts.find(cuffs);it!=counts.end())b.priorCuffs=std::max(0,it->second);
    if(prop){
        b.post=prop->GetFormID();const float yaw=b.angle.z;
        prop->SetPosition(b.position+RE::NiPoint3{std::cos(yaw)*80.f,-std::sin(yaw)*80.f,0});prop->SetAngle(b.angle);
    }
    {std::scoped_lock lock(mutex);bindings.emplace(actor->GetFormID(),b);}
    Diagnostics::Trace("Binding created target={:08X} post={:08X} pacified={} position=({},{},{})",actor->GetFormID(),b.post,b.pacified,b.position.x,b.position.y,b.position.z);
    if(pacify)Wardrobe::Capture(actor,false);
    actor->AddObjectToContainer(cuffs,nullptr,1,nullptr);
    RE::ActorEquipManager::GetSingleton()->EquipObject(actor,cuffs,nullptr,1,nullptr,false,true,false,true);
    actor->StopCombat();actor->EnableAI(true);actor->SetLifeState(RE::ACTOR_LIFE_STATE::kRestrained);actor->GetActorRuntimeData().boolFlags.set(flagsMask);
    actor->NotifyAnimationGraph("SprintStop");actor->NotifyAnimationGraph("moveStop");
    if(!Crawl::BindBound(actor))actor->NotifyAnimationGraph("IdleBoundStandingStart");
    if(!pacify){HoldAnchor(actor);StartTether(actor,true);}
    spdlog::info("Bound ref={:08X} type={} cell={:08X} teammate={}",actor->GetFormID(),pacify?"capture":"fixed-human",b.cell,b.teammate);
    Notify(pacify?"目标已捕获并捆绑":"坐骑已恢复人形，正在连接固定桩绳索");return true;
}

struct Result final:RE::BSScript::IStackCallbackFunctor {
    std::function<void(bool)> done;
    bool holder;
    Diagnostics::Context context=Diagnostics::current;
    std::uint64_t request=++Diagnostics::sequence;
    const char* function;bool trace; // Call sites use static function-name literals.
    explicit Result(std::function<void(bool)> fn,bool owner,const char* name,bool record):done(std::move(fn)),holder(owner),function(name),trace(record){}
    void operator()(RE::BSScript::Variable result)override{
        // A missing/invalid Boolean result is not a successful disconnect.
        // Leave the request pending for the bounded watchdog instead.
        Diagnostics::Scope operation(context);
        if(holder ? (!result.IsObject()&&!result.IsNoneObject()) : !result.IsBool()){
            Diagnostics::Trace("VM callback request={} function={} invalid-type; existing watchdog retained",request,function);return;
        }
        auto fn=done;const bool value=holder ? (result.IsObject()&&result.Unpack<RE::Actor*>()==RE::PlayerCharacter::GetSingleton()) : result.IsBool()&&result.GetBool();
        if(trace)Diagnostics::Trace("VM callback request={} function={} value={}",request,function,value);
        SKSE::GetTaskInterface()->AddTask([fn=std::move(fn),value,context=context]{Diagnostics::Scope operation(context);fn(value);});
    }
    void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&)override{}
};
template<class... Args> bool Call(const char* function,std::function<void(bool)> done,Args... args){
    const bool query=std::string_view(function)=="IsLeashed"||std::string_view(function)=="GetLeashHolder";
    const bool trace=!query||Diagnostics::Sample(4,2000);
    auto* callback=new Result(std::move(done),std::string_view(function)=="GetLeashHolder",function,trace);
    const auto request=callback->request;
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result{callback};
    if(trace)Diagnostics::Trace("VM request={} function={}",request,function);
    const bool dispatched=RE::BSScript::Internal::VirtualMachine::GetSingleton()->DispatchStaticCall("LeashFramework",function,RE::MakeFunctionArguments(std::move(args)...),result);
    if(trace||!dispatched)Diagnostics::Trace("VM dispatch request={} function={} accepted={}",request,function,dispatched);
    return dispatched;
}
bool Current(RE::FormID id,std::uint64_t token){const auto it=tethers.find(id);return it!=tethers.end()&&it->second.token==token;}
void FailTether(Tether& tether){
    if(tether.phase==Phase::Failed)return;
    tether.phase=Phase::Failed;tether.wanted=false;
    Notify("牵引接口未完成，已停止自动重试；人魅-解除坐骑化与束缚可还原本地状态，绳索清理尚未确认");
}
void StopTether(RE::Actor* actor){
    if(!actor)return;const auto id=actor->GetFormID();auto it=tethers.find(id);if(it==tethers.end())return;
    it->second.wanted=false;if(it->second.phase!=Phase::Active||it->second.retry>0)return;
    if(!leashAvailable){tethers.erase(it);return;}
    it->second.phase=Phase::Stopping;it->second.pending=true;it->second.elapsed=0;const auto token=it->second.token;
    if(!Call("DisconnectLeash",[id,token](bool removed){
        Diagnostics::Trace("Disconnect callback target={:08X} token={} removed={} current={}",id,token,removed,Current(id,token));
        if(!Current(id,token))return;
        if(removed){
            const bool anchor=tethers.at(id).anchorAfterDisconnect;tethers.erase(id);
            if(anchor)if(auto* ref=RE::TESForm::LookupByID<RE::Actor>(id))Bind(ref,false);
            return;
        }
        auto& t=tethers.at(id);t.pending=false;t.phase=Phase::Reconciling;t.elapsed=0;t.retry=1.f;
        if(++t.failures>=3)FailTether(t);
    },it->second.anchor?static_cast<RE::Actor*>(nullptr):RE::PlayerCharacter::GetSingleton()->As<RE::Actor>(),actor)){
        it->second.pending=false;it->second.phase=Phase::Active;it->second.retry=1.f;
        if(++it->second.failures>=3)FailTether(it->second);
    }
}
void StartTether(RE::Actor* actor,bool anchor){
    if(!leashAvailable){Notify("人魅-牵引坐骑需要启用LeashFramework及Leash.esm");return;}
    if(!Eligible(actor)||(anchor?!Bound(actor):!Mounts::Converted(actor))||(!anchor&&Bound(actor))||Busy(actor)||Mounts::Riding(actor)){
        Notify("目标状态不允许连接牵引绳");return;
    }
    const auto id=actor->GetFormID();const auto handle=actor->GetHandle();const auto token=++serial;
    Diagnostics::Trace("Tether start target={:08X} token={} anchor={}",id,token,anchor);
    tethers.emplace(id,Tether{Phase::Checking,true,token});tethers.at(id).born=leashEventsSeen.load();tethers.at(id).pending=true;tethers.at(id).anchor=anchor;
    if(!Call("IsLeashed",[id,handle,token](bool occupied){
        Diagnostics::Trace("Tether occupancy target={:08X} token={} occupied={} current={}",id,token,occupied,Current(id,token));
        if(!Current(id,token))return;auto ref=handle.get();
        const bool anchor=tethers.at(id).anchor;
        if(occupied||!ref||!tethers.at(id).wanted||!Eligible(ref.get())||(anchor?!Bound(ref.get()):!Mounts::Converted(ref.get()))){tethers.erase(id);if(anchor&&ref)Release(ref.get());if(occupied)Notify("目标已有其他牵引，未覆盖；请先解除原牵引");return;}
        tethers.at(id).phase=Phase::Applying;tethers.at(id).elapsed=0;
        auto complete=[id,handle,token,anchor](bool applied){
            Diagnostics::Trace("Tether apply target={:08X} token={} anchor={} applied={} current={}",id,token,anchor,applied,Current(id,token));
            if(!Current(id,token))return;
            if(!applied){tethers.erase(id);if(anchor)if(auto ref=handle.get())Release(ref.get());Notify("LeashFramework未能创建牵引绳，固定已撤回，请查看日志");return;}
            auto ref=handle.get();tethers.at(id).phase=Phase::Active;tethers.at(id).pending=false;tethers.at(id).elapsed=0;
            if(!ref){tethers.erase(id);return;}
            if(!tethers.at(id).wanted||!Eligible(ref.get())||(anchor?!Bound(ref.get()):!Mounts::Converted(ref.get()))){StopTether(ref.get());return;}
            // Keep humanoid crawl locomotion: no ragdoll pull.
            Call("SetRagdollOverride",[](bool){},ref.get(),0);
            if(!anchor){
                Call("SetLeashFollower",[](bool){},ref.get(),0);
                Call("SetPreventOverstretchOverride",[](bool){},ref.get(),0);
            }
            Notify(anchor?"坐骑已通过绳索连接固定桩":"已连接牵引绳；再次施放人魅-牵引坐骑可解除");
        };
        bool dispatched=false;
        if(anchor){
            Binding b;
            auto* post=Find(id,b)?RE::TESForm::LookupByID<RE::TESObjectREFR>(b.post):nullptr;
            if(post&&SameSpace(post->GetParentCell(),ref->GetParentCell())){
                const auto p=post->GetPosition()+RE::NiPoint3{0,0,48};
                dispatched=Call("ApplyStandaloneLeashAtPosition",complete,ref.get(),post->GetParentCell(),p.x,p.y,p.z,std::string("Leash/neck_rope.nif"),std::string("NPC Spine2 [Spn2]"),std::string("Leash1_0"),std::string("Leash1_1_"),100.f,220.f,true,0.f,0.f,8.f);
            }
        }else dispatched=Call("ApplyStandaloneLeash",complete,RE::PlayerCharacter::GetSingleton()->As<RE::Actor>(),ref.get(),std::string("Leash/neck_rope.nif"),std::string("NPC Spine2 [Spn2]"),std::string("Leash1_0"),std::string("Leash1_1_"),100.f,220.f,true,true,0.f,0.f,8.f);
        if(!dispatched){
            tethers.erase(id);if(anchor)Release(ref.get());Notify("牵引接口调用失败，未进入牵引状态");
        }
    },actor)){tethers.erase(id);Notify("无法访问LeashFramework脚本接口");}
}
void Unleashed(RE::FormID id,std::uint64_t epoch,std::uint64_t sequence){
    Diagnostics::Trace("Unleash event target={:08X} epoch={} sequence={} currentEpoch={}",id,epoch,sequence,session.load());
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
bool BlockMovement(RE::Actor* actor){
    Binding b;
    if(!actor||!Find(actor->GetFormID(),b)||b.releaseRequested||actor->IsDead()||!Ready(actor))return false;
    if(!b.pacified&&(!b.post||Displaced(actor,b)))return false;
    Freeze(actor,b); // Cancel residual velocity before, not only after, native movement.
    return true;
}
bool HoldAnchor(RE::Actor* actor){
    if(!actor)return false;
    {std::scoped_lock lock(mutex);auto it=bindings.find(actor->GetFormID());if(it==bindings.end()||!it->second.post)return false;
        if(it->second.releaseRequested||!Ready(actor)||Displaced(actor,it->second))return true;
        if(!it->second.teammateSaved){it->second.teammate=actor->IsPlayerTeammate();it->second.teammateSaved=true;}}
    if(!actor->IsAIEnabled())actor->EnableAI(true);
    actor->GetActorRuntimeData().boolFlags.set(RE::Actor::BOOL_FLAGS::kForceAnimGraphUpdate);
    actor->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
    return true;
}
bool Leashed(RE::Actor* actor){if(!actor)return false;auto it=tethers.find(actor->GetFormID());return it!=tethers.end()&&!it->second.anchor&&it->second.phase==Phase::Active&&it->second.wanted;}
bool Busy(RE::Actor* actor){return actor&&tethers.contains(actor->GetFormID());}
bool Release(RE::Actor* actor){
    if(!actor)return true;
    {std::scoped_lock lock(mutex);auto it=bindings.find(actor->GetFormID());if(it!=bindings.end())it->second.releaseRequested=true;}
    if(Busy(actor)){tethers.at(actor->GetFormID()).anchorAfterDisconnect=false;StopTether(actor);if(Busy(actor)&&tethers.at(actor->GetFormID()).phase!=Phase::Failed)return false;}
    Binding b;const auto id=actor->GetFormID();if(!Find(id,b))return true;
    if(!Ready(actor)){std::scoped_lock lock(mutex);bindings.at(id).releaseRequested=true;return false;}
    {std::scoped_lock lock(mutex);bindings.erase(id);}
    if(cuffs){
        auto* equip=RE::ActorEquipManager::GetSingleton();equip->UnequipObject(actor,cuffs,nullptr,1,nullptr,false,true,false,true);
        const auto counts=actor->GetInventoryCounts([&](RE::TESBoundObject& object){return &object==cuffs;});
        if(auto it=counts.find(cuffs);it!=counts.end()&&it->second>static_cast<int>(b.priorCuffs))actor->RemoveItem(cuffs,1,RE::ITEM_REMOVE_REASON::kRemove,nullptr,nullptr);
        if(auto* old=RE::TESForm::LookupByID<RE::TESObjectARMO>(b.oldHands))equip->EquipObject(actor,old,nullptr,1,nullptr,false,false,false,true);
    }
    if(b.pacified)Wardrobe::Restore(actor);
    Crawl::Remove(id);
    Diagnostics::Trace("Binding released target={:08X} post={:08X} pacified={} ropePending={}",id,b.post,b.pacified,Busy(actor));
    DeletePost(b.post);auto& flags=actor->GetActorRuntimeData().boolFlags;flags.reset(flagsMask);flags.set(static_cast<RE::Actor::BOOL_FLAGS>(b.flags));
    if(b.teammateSaved){auto& bits=actor->GetActorRuntimeData().boolBits;bits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);if(b.teammate)bits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);}
    if(!actor->IsDead()&&(actor->AsActorState()->GetLifeState()==RE::ACTOR_LIFE_STATE::kRestrained||
        actor->AsActorState()->GetLifeState()==RE::ACTOR_LIFE_STATE::kAlive))actor->SetLifeState(static_cast<RE::ACTOR_LIFE_STATE>(b.life));
    actor->EnableAI(b.ai);
    if(!actor->IsDead()){actor->NotifyAnimationGraph("IdleForceDefaultState");if(b.ai)actor->EvaluatePackage(false,false);}return true;
}
void ReleaseAll(){
    std::vector<RE::FormID> ids;{std::scoped_lock lock(mutex);for(auto& [id,b]:bindings){b.releaseRequested=true;ids.push_back(id);}}
    for(auto& [id,t]:tethers){t.wanted=false;if(std::find(ids.begin(),ids.end(),id)==ids.end())ids.push_back(id);}
    for(auto id:ids)if(auto* actor=RE::TESForm::LookupByID<RE::Actor>(id))Release(actor);
}
void AfterActorUpdate(RE::Actor* actor){Binding b;if(hooksReady&&Find(actor->GetFormID(),b)&&!actor->IsDead())Freeze(actor,b);}
void CheckTether(RE::Actor* actor){
    const auto id=actor->GetFormID();auto& tether=tethers.at(id);
    if(tether.phase!=Phase::Active&&tether.phase!=Phase::Reconciling)return;
    if(tether.query||tether.pending||tether.retry>0)return;
    tether.query=true;tether.elapsed=0;const auto token=tether.token;
    if(!Call(tether.anchor?"IsLeashed":"GetLeashHolder",[id,token](bool owned){
        if(!Current(id,token))return;auto& t=tethers.at(id);t.query=false;
        if(t.phase!=Phase::Active&&t.phase!=Phase::Reconciling&&t.phase!=Phase::Failed)return;
        if(t.pending)return;
        if(!owned){tethers.erase(id);return;}
        t.phase=Phase::Active;t.elapsed=0;
        if(t.wanted)t.failures=0; // A positive ownership query does not undo failed disconnect attempts.
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
    AdvanceTethers(delta);
    std::vector<std::pair<RE::FormID,Binding>> work;{std::scoped_lock lock(mutex);for(const auto& item:bindings)work.push_back(item);}
    for(const auto& [id,b]:work)if(auto* actor=RE::TESForm::LookupByID<RE::Actor>(id)){
        if(!Ready(actor))continue;
        if(actor->IsDead()||b.releaseRequested){Release(actor);continue;}
        if(b.post){
            // A failed preflight, external disconnect or a missing saved rope
            // must not leave an invisible, permanent movement restriction.
            if((!b.pacified&&!Busy(actor))||(Busy(actor)&&tethers.at(id).phase==Phase::Failed)){Release(actor);continue;}
            auto* cell=actor->GetParentCell();
            if(Displaced(actor,b)){
                if(cell&&cell->IsAttached()&&actor->Get3D()){
                    spdlog::warn("Anchor bypassed ref={:08X} oldCell={:08X} newCell={:08X}; releasing without cross-cell MoveTo",id,b.cell,cell->GetFormID());
                    const auto shift=actor->GetPosition()-b.position;
                    Diagnostics::Trace("Anchor displacement target={:08X} delta=({},{},{})",id,shift.x,shift.y,shift.z);
                    Release(actor);Notify("其他移动绕过固定，已安全解除固定；未强制传回旧地点");
                }continue;
            }
            HoldAnchor(actor);
        }
        actor->GetActorRuntimeData().boolFlags.set(flagsMask);
        if(actor->AsActorState()->GetLifeState()==RE::ACTOR_LIFE_STATE::kAlive)actor->SetLifeState(RE::ACTOR_LIFE_STATE::kRestrained);
        Freeze(actor,b);
        Crawl::UpdateBound(actor,delta);
        if(b.pacified&&actor->IsInCombat())actor->StopCombat();
    }
    poll-=delta;if(poll>0)return;poll=1.f;
    std::vector<RE::FormID> ids;for(const auto& [id,t]:tethers)ids.push_back(id);
    for(auto id:ids){auto it=tethers.find(id);if(it==tethers.end())continue;auto* actor=RE::TESForm::LookupByID<RE::Actor>(id);if(!actor)continue;
        if(it->second.phase==Phase::Failed)continue;
        if(it->second.phase==Phase::Reconciling){if((it->second.anchor?!Bound(actor):!Mounts::Converted(actor)))it->second.wanted=false;CheckTether(actor);continue;}
        if(!it->second.wanted||actor->IsDead()||(it->second.anchor?!Bound(actor):!Mounts::Converted(actor))){StopTether(actor);continue;}
        if(it->second.phase==Phase::Active)CheckTether(actor);
    }
}
void ToggleTether(RE::Actor* actor,bool releaseOnly=false){
    if(Busy(actor)){
        auto& t=tethers.at(actor->GetFormID());t.anchorAfterDisconnect=false;
        if(t.phase==Phase::Failed&&!t.pending&&!t.query){t.phase=Phase::Reconciling;t.failures=0;t.retry=0;}
        if(t.anchor){Release(actor);return;}
        StopTether(actor);return;
    }
    // A binding may outlive its asynchronous rope cleanup. Finish release, not a new rope.
    if(Bound(actor)){Release(actor);return;}
    if(releaseOnly)Notify("目标没有本Mod创建的牵引");else StartTether(actor);
}
bool Handle(RE::FormID effect,RE::Actor* actor){
    Diagnostics::Scope operation(actor?actor->GetFormID():0);
    Diagnostics::Trace("Spell handling effect={:08X}",effect);
    for(unsigned i=0;i<effects.size();++i)if(effects[i]&&effects[i]->GetFormID()==effect){
        if(i==0)Bind(actor,false);else if(i==1){
            if(actor&&actor->IsPlayerRef()){auto* pick=RE::CrosshairPickData::GetSingleton();auto ref=pick?pick->GetActiveTarget().get():RE::NiPointer<RE::TESObjectREFR>{};actor=ref?ref->As<RE::Actor>():nullptr;}
            Bind(actor,true);
        }else ToggleTether(actor,i==3);
        return true;
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
        Hook{reinterpret_cast<void*>(REL::RelocationID(36537,37537).address()),reinterpret_cast<void*>(HostileHook),reinterpret_cast<void**>(&originalHostile)},
        Hook{reinterpret_cast<void*>(REL::RelocationID(37608,38561).address()),reinterpret_cast<void*>(CombatHook),reinterpret_cast<void**>(&originalCombat)}};
    for(const auto& h:hooks){const auto result=MH_CreateHook(h.address,h.replacement,h.original);if(result!=MH_OK){spdlog::error("Restraint detour failed {}",int(result));return;}MH_QueueEnableHook(h.address);}
    if(MH_ApplyQueued()!=MH_OK){spdlog::error("Restraint detour activation failed");return;}
    hooksReady=true; // Do not hook SetPosition: cell loading must keep its own coordinate transaction intact.
    spdlog::info("C/D restraint hooks ready; E/F LeashFramework available={}",leashAvailable);
}
void Save(SKSE::SerializationInterface* save){
    {std::scoped_lock lock(mutex);save->OpenRecord('BIND',2);save->WriteRecordData(static_cast<std::uint32_t>(bindings.size()));
    for(const auto& [id,b]:bindings){for(auto v:{id,b.post,b.cell,b.flags,b.life,std::uint32_t(b.ai),b.oldHands,b.priorCuffs,std::uint32_t(b.pacified),std::uint32_t(b.releaseRequested),std::uint32_t(b.teammate),std::uint32_t(b.teammateSaved)})save->WriteRecordData(v);
        for(float v:{b.position.x,b.position.y,b.position.z,b.angle.x,b.angle.y,b.angle.z})save->WriteRecordData(v);}}
    // A preflight check has not created anything: never claim an unrelated rope on load.
    save->OpenRecord('TETH',2);save->WriteRecordData(static_cast<std::uint32_t>(std::count_if(tethers.begin(),tethers.end(),[](const auto& pair){return pair.second.phase!=Phase::Checking;})));
    for(const auto& [id,t]:tethers)if(t.phase!=Phase::Checking){save->WriteRecordData(id);save->WriteRecordData(std::uint32_t(t.wanted));save->WriteRecordData(std::uint32_t(t.anchor));}
}
bool Load(SKSE::SerializationInterface* save,std::uint32_t type,std::uint32_t version,std::uint32_t length){
    if(type!='BIND'&&type!='TETH')return false;if(version<1||version>2||length<4)return true;
    const auto stride=type=='BIND'?(version==1?64u:72u):(version==1?8u:12u);std::uint32_t count=0;if(save->ReadRecordData(&count,4)!=4||length!=4+std::uint64_t(count)*stride)return true;
    for(unsigned i=0;i<count;++i){
        if(type=='TETH'){std::uint32_t id=0,wanted=0,resolved=0,anchor=0;if(save->ReadRecordData(&id,4)!=4||save->ReadRecordData(&wanted,4)!=4||(version==2&&save->ReadRecordData(&anchor,4)!=4))break;if(save->ResolveFormID(id,resolved)){tethers[resolved]={Phase::Reconciling,wanted!=0,++serial};tethers[resolved].born=leashEventsSeen.load();tethers[resolved].anchor=anchor!=0;}continue;}
        std::array<std::uint32_t,12> values{};std::array<float,6> coordinates;const auto valueBytes=version==1?40u:48u;
        if(save->ReadRecordData(values.data(),valueBytes)!=valueBytes||save->ReadRecordData(coordinates.data(),24)!=24)break;
        if(!std::all_of(coordinates.begin(),coordinates.end(),[](float n){return std::isfinite(n);})||(values[4]!=0&&values[4]!=6))continue;
        RE::FormID id;if(!save->ResolveFormID(values[0],id))continue;
        Binding b;save->ResolveFormID(values[1],b.post);save->ResolveFormID(values[2],b.cell);save->ResolveFormID(values[6],b.oldHands);
        b.flags=values[3]&static_cast<unsigned>(flagsMask);b.life=values[4];b.ai=values[5]!=0;b.priorCuffs=values[7];b.pacified=values[8]!=0;b.releaseRequested=values[9]!=0||(!b.cell&&!b.pacified)||(values[1]&&!b.post);
        b.teammate=values[10]!=0;b.teammateSaved=values[11]!=0;
        b.position={coordinates[0],coordinates[1],coordinates[2]};b.angle={coordinates[3],coordinates[4],coordinates[5]};std::scoped_lock lock(mutex);bindings[id]=b;
    }return true;
}
void Revert(){std::scoped_lock lock(mutex);++session;bindings.clear();tethers.clear();++serial;poll=0;}
std::uint64_t Session(){return session.load();}
void Deleted(RE::FormID id){Binding b;if(Find(id,b)){{std::scoped_lock lock(mutex);bindings.erase(id);}DeletePost(b.post);}tethers.erase(id);}
void Loaded(RE::Actor* actor){
    Binding b;if(!actor||!Ready(actor)||!Find(actor->GetFormID(),b))return;
    if(!hooksReady||actor->IsDead()||b.releaseRequested){Release(actor);return;}
    if(b.post&&Displaced(actor,b))return; // Tick releases after the destination is attached.
    actor->EnableAI(true);actor->SetLifeState(RE::ACTOR_LIFE_STATE::kRestrained);actor->GetActorRuntimeData().boolFlags.set(flagsMask);Freeze(actor,b);
    if(b.post){HoldAnchor(actor);if(!b.pacified&&leashAvailable&&!Busy(actor))StartTether(actor,true);}
    if(b.pacified&&!Mounts::Converted(actor)&&actor->Get3D())if(!Crawl::BindBound(actor))actor->NotifyAnimationGraph("IdleBoundStandingStart");
}
void PostLoad(){
    if(!leashAvailable)tethers.clear();
    if(!hooksReady){ReleaseAll();return;}
    std::vector<std::pair<RE::FormID,Binding>> work;{std::scoped_lock lock(mutex);for(const auto& entry:bindings)work.push_back(entry);}
    for(const auto& [id,b]:work)Loaded(RE::TESForm::LookupByID<RE::Actor>(id));
}
}
