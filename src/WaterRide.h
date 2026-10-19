#pragma once
#include "Crawl.h"
#include <algorithm>
#include <cmath>

namespace WaterRide {
inline RE::SpellItem* ability=nullptr;
inline float depth=0.f;
inline RE::FormID savedActor=0;
inline float savedDepth=0.f;
inline void Remove(RE::Actor* actor){
    if(actor&&ability&&actor->HasSpell(ability))actor->RemoveSpell(ability);
}
inline void Begin(RE::Actor* player,RE::Actor* mount){
    depth=0.f;
    if(!ability)return;
    for(auto* actor:{player,mount})if(actor&&!actor->HasSpell(ability))actor->AddSpell(ability);
}
inline void Raise(RE::Actor* actor,float offset){if(actor&&actor->Get3D()&&offset>0.f){auto position=actor->GetPosition();position.z+=offset;actor->SetPosition(position,true);}}
inline void End(RE::Actor* player,RE::Actor* mount){Raise(mount,depth);Remove(player);Remove(mount);depth=0.f;}
inline float Offset(){return depth;}
inline void Reset(){depth=savedDepth=0.f;savedActor=0;}
inline void Recover(RE::Actor* actor){
    if(actor&&actor->GetFormID()==savedActor&&actor->Get3D()){Raise(actor,savedDepth);savedActor=0;savedDepth=0;}
}
inline void Save(SKSE::SerializationInterface* save,RE::Actor* mount){
    const auto id=mount?mount->GetFormID():savedActor;const float offset=mount?depth:savedDepth;
    if(id&&offset>0.f&&save->OpenRecord('WATR',1)){save->WriteRecordData(id);save->WriteRecordData(offset);}
}
inline bool Load(SKSE::SerializationInterface* save,std::uint32_t type,std::uint32_t version,std::uint32_t length){
    if(type!='WATR')return false;
    std::uint32_t id=0,resolved=0;float offset=0;
    if(version==1&&length==8&&save->ReadRecordData(&id,4)==4&&save->ReadRecordData(&offset,4)==4&&std::isfinite(offset)&&offset>0&&save->ResolveFormID(id,resolved)){
        savedActor=resolved;savedDepth=offset;
    }return true;
}
inline void Update(RE::Actor* player,RE::Actor* mount,float delta){
    auto* controller=player->GetCharController();auto* root=mount->Get3D();
    if(!controller||!root){depth=0.f;return;}
    const bool air=controller->context.currentState==RE::hkpCharacterStateType::kInAir||controller->context.currentState==RE::hkpCharacterStateType::kJumping;
    // Water walking is OnGround, not Swimming. Detect the actual local water plane.
    const float water=player->GetWaterHeight();
    const bool onWater=!air&&std::isfinite(water)&&player->GetPosition().z<=water+4.f;
    RE::NiPoint3 back;float target=0.f;
    if(onWater&&Crawl::Anchor(mount,back,0.f))target=std::max(0.f,back.z)*root->world.scale/3.f;
    const float blend=std::min(std::max(delta,0.f)/.15f,1.f);
    depth+=(target-depth)*blend;
    if(std::abs(depth-target)<.01f)depth=target;
}
}
