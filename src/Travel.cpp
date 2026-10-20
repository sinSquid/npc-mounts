#include "pch.h"
#include "Travel.h"
#include "TravelState.h"
#include "Extras.h"
#include <numbers>
#include <atomic>
namespace Mounts {
bool Converted(RE::Actor*);
bool Restoring(RE::Actor*);
bool Riding(RE::Actor*);
bool ActorReady(RE::Actor*);
bool Ray(RE::bhkWorld*,const RE::NiPoint3&,const RE::NiPoint3&,RE::bhkPickData&);
}
namespace Travel {
RE::FormID last=0;
Tracker tracker;
float probe=0;
std::atomic<bool> armed=false,arrived=false;
struct Arrival final:RE::BSTEventSink<RE::PositionPlayerEvent>{
    RE::BSEventNotifyControl ProcessEvent(const RE::PositionPlayerEvent* event,RE::BSTEventSource<RE::PositionPlayerEvent>*)override{
        if(event&&event->type==RE::PositionPlayerEvent::EVENT_TYPE::kFinish&&armed.load())arrived.store(true);
        return RE::BSEventNotifyControl::kContinue;
    }
} arrival;
void Initialize(){auto* source=RE::PlayerCharacter::GetSingleton()->AsPositionPlayerEventSource();source->RemoveEventSink(&arrival);source->AddEventSink(&arrival);}
void Remember(RE::Actor* actor){last=actor->GetFormID();tracker.Done();probe=0;spdlog::info("Travel selected ONLY ref={:08X}",last);}
void Forget(RE::FormID id){if(last==id){last=0;tracker.Done();}}
void Revert(){armed=false;arrived=false;last=0;tracker={};probe=0;}
void Save(SKSE::SerializationInterface* save){if(last){save->OpenRecord('TRVL',1);save->WriteRecordData(last);}}
bool Load(SKSE::SerializationInterface* save,std::uint32_t type,std::uint32_t version,std::uint32_t length){
    if(type!='TRVL')return false;
    armed=false;arrived=false;last=0;tracker={};probe=0;RE::FormID id{};
    if(version==1&&length==4&&save->ReadRecordData(&id,4)==4)save->ResolveFormID(id,last);
    return true;
}
bool Landing(RE::Actor* player,RE::Actor* actor,RE::NiPoint3& result){
    auto* cc=player->GetCharController();auto* world=cc?cc->GetHavokWorld():nullptr;if(!world)return false;
    const auto origin=player->GetPosition();const float yaw=player->GetAngleZ();
    const float radius=std::clamp(96.f*actor->GetScale(),80.f,240.f);
    for(float distance:{radius,radius*.6f})for(unsigned i=0;i<8;++i){
        const float angle=yaw+std::numbers::pi_v<float>+i*std::numbers::pi_v<float>/4;
        const RE::NiPoint3 point=origin+RE::NiPoint3{std::sin(angle)*distance,std::cos(angle)*distance,0};
        RE::bhkPickData wall{},floor{};
        if(Mounts::Ray(world,origin+RE::NiPoint3{0,0,48},point+RE::NiPoint3{0,0,48},wall))continue;
        const auto top=point+RE::NiPoint3{0,0,64},bottom=point-RE::NiPoint3{0,0,160};
        if(!Mounts::Ray(world,top,bottom,floor))continue;
        result=top+(bottom-top)*floor.rayOutput.hitFraction;return true;
    }
    return false;
}
void Relocate(RE::Actor* actor,RE::Actor* player,const RE::NiPoint3& position){
    // Same engine call as CommonLib TESObjectREFR::MoveTo; explicit destination
    // gives a nearby landing without MoveTo-then-SetPosition on queued 3D.
    using Move=void(*)(RE::TESObjectREFR*,const RE::ObjectRefHandle&,RE::TESObjectCELL*,RE::TESWorldSpace*,const RE::NiPoint3&,const RE::NiPoint3&);
    static REL::Relocation<Move> move{RELOCATION_ID(56227,56626)};
    const auto handle=player->CreateRefHandle();
    move(actor,handle,player->GetParentCell(),player->GetWorldspace(),position,actor->GetAngle());
}
void Tick(RE::Actor* player,float delta){
    auto* ui=RE::UI::GetSingleton();
    if(ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)||!Mounts::ActorReady(player)){tracker.Gap();return;}
    if(ui->GameIsPaused())return;
    if(arrived.exchange(false))tracker.Arrived();
    auto* cell=player->GetParentCell();auto* world=player->GetWorldspace();const auto p=player->GetPosition();
    const bool ready=tracker.Observe({cell->GetFormID(),world?world->GetFormID():0,cell->IsInteriorCell(),p.x,p.y,p.z},delta);
    armed=true;if(!ready)return;
    probe-=std::clamp(delta,0.f,.1f);if(probe>0)return;probe=.25f;
    if(!last){tracker.Done();return;}
    auto* actor=RE::TESForm::LookupByID<RE::Actor>(last);
    if(!actor)return; // Unresolved/unloaded reference: bounded by the tracker timeout.
    if(!Mounts::Converted(actor)||Mounts::Restoring(actor)||actor->IsDead()||actor->IsDeleted()){Forget(last);return;}
    // Fixed/leashed targets belong to their existing owner; do not silently undo it.
    if(Extras::Bound(actor)||Extras::Busy(actor)||Mounts::Riding(actor)||actor->IsDisabled()){
        spdlog::info("Travel selected skipped ref={:08X}: fixed/busy/ridden/disabled; no fallback",last);
        tracker.Done();return;
    }
    if(player->IsDead()||player->IsInKillMove()||actor->IsInKillMove()||actor->IsOnMount())return;
    if(actor->Get3D()&&!Mounts::ActorReady(actor))return; // Source is currently unloading.
    if(actor->GetParentCell()==cell&&(actor->GetPosition()-p).Length()<384.f){tracker.Done();return;}
    RE::NiPoint3 landing;if(!Landing(player,actor,landing))return;
    tracker.Done(); // Consume before engine call: never recurse or pull back every frame.
    Relocate(actor,player,landing);
    spdlog::info("Travel companion relocation requested ref={:08X} cell={:08X}",last,cell->GetFormID());
}
}
