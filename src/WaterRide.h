#pragma once
#include "Crawl.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace WaterRide {
inline RE::SpellItem* ability=nullptr;
inline float depth=0.f;
inline RE::FormID activeActor=0;
inline std::unordered_map<RE::FormID,float> pending;
inline void Remove(RE::Actor* actor){
    if(actor&&ability&&actor->HasSpell(ability))actor->RemoveSpell(ability);
}
inline void Begin(RE::Actor* player,RE::Actor* mount){
    depth=0.f;activeActor=mount?mount->GetFormID():0;
    if(!ability)return;
    for(auto* actor:{player,mount})if(actor&&!actor->HasSpell(ability))actor->AddSpell(ability);
}
inline bool Raise(RE::Actor* actor,float offset){if(!actor||!actor->Get3D())return false;if(offset>0.f){auto position=actor->GetPosition();position.z+=offset;actor->SetPosition(position,true);}return true;}
inline void End(RE::Actor* player,RE::Actor* mount){
    const auto id=mount?mount->GetFormID():activeActor;
    if(id&&depth>0.f&&!Raise(mount,depth))pending[id]+=depth;
    Remove(player);Remove(mount);depth=0.f;activeActor=0;
}
inline float Offset(){return depth;}
inline void Reset(){depth=0.f;activeActor=0;pending.clear();}
inline void Forget(RE::FormID id){pending.erase(id);if(activeActor==id)activeActor=0;}
inline void Recover(RE::Actor* actor){
    if(!actor)return;
    const auto it=pending.find(actor->GetFormID());
    if(it!=pending.end()&&Raise(actor,it->second))pending.erase(it);
}
inline void Save(SKSE::SerializationInterface* save,RE::Actor* mount){
    const auto current=mount?mount->GetFormID():activeActor;
    for(const auto& [id,offset]:pending)if(id!=current&&save->OpenRecord('WATR',1)){save->WriteRecordData(id);save->WriteRecordData(offset);}
    if(current&&depth>0.f&&save->OpenRecord('WATR',1)){save->WriteRecordData(current);save->WriteRecordData(depth);}
}
inline bool Load(SKSE::SerializationInterface* save,std::uint32_t type,std::uint32_t version,std::uint32_t length){
    if(type!='WATR')return false;
    std::uint32_t id=0,resolved=0;float offset=0;
    if(version==1&&length==8&&save->ReadRecordData(&id,4)==4&&save->ReadRecordData(&offset,4)==4&&std::isfinite(offset)&&offset>0&&save->ResolveFormID(id,resolved)){
        pending.insert_or_assign(resolved,offset);
    }return true;
}
inline void Update(RE::Actor* player,RE::Actor* mount,float delta){
    auto* controller=player->GetCharController();auto* root=mount->Get3D();
    if(!controller||!root||!mount->GetCharController())return;
    const bool air=controller->context.currentState==RE::hkpCharacterStateType::kInAir||controller->context.currentState==RE::hkpCharacterStateType::kJumping;
    // Water walking is OnGround, not Swimming. Detect the actual local water plane.
    const float water=player->GetWaterHeight();
    const bool onWater=!air&&std::isfinite(water)&&player->GetPosition().z<=water+4.f;
    RE::NiPoint3 back;float target=0.f;
    if(onWater&&Crawl::Anchor(mount,back,0.f))target=std::max(0.f,back.z)*root->world.scale/3.f;
    const float blend=1.f-std::exp(-std::max(delta,0.f)/.15f);
    depth+=(target-depth)*blend;
    if(std::abs(depth-target)<.01f)depth=target;
}
}
