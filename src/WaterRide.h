#pragma once
#include "Crawl.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace WaterRide {
inline RE::SpellItem* ability=nullptr;
inline float depth=0.f;
inline RE::FormID activeActor=0;
struct Recovery {RE::FormID cell=0;RE::NiPoint3 lowered{};float depth=0.f;};
inline Recovery applied;
inline std::unordered_map<RE::FormID,Recovery> pending;
inline void Remove(RE::Actor* actor){
    if(actor&&ability&&actor->HasSpell(ability))actor->RemoveSpell(ability);
}
inline void Begin(RE::Actor* player,RE::Actor* mount){
    depth=0.f;applied={};activeActor=mount?mount->GetFormID():0;
    if(!ability)return;
    for(auto* actor:{player,mount})if(actor&&!actor->HasSpell(ability))actor->AddSpell(ability);
}
// Record only after SyncMount has applied the reference/controller position.
inline void Applied(RE::Actor* actor){
    auto* cell=actor->GetParentCell();
    applied=cell?Recovery{cell->GetFormID(),actor->GetPosition(),depth}:Recovery{};
}
inline bool Raise(RE::Actor* actor,const Recovery& saved){
    if(!actor||!actor->Get3D()||!actor->GetParentCell())return false;
    if(saved.depth<=0.f)return true;
    auto position=actor->GetPosition();
    const float surface=saved.lowered.z+saved.depth;
    // Native support may already have raised the actor. Never add the debt to
    // its new position or pull back an externally moved/teleported reference.
    if(actor->GetParentCell()->GetFormID()==saved.cell&&
        std::hypot(position.x-saved.lowered.x,position.y-saved.lowered.y)<=.5f&&
        position.z>=saved.lowered.z-.5f&&position.z<surface){
        position.z=surface;actor->SetPosition(position,true);
    }
    return true;
}
inline void End(RE::Actor* player,RE::Actor* mount){
    const auto id=mount?mount->GetFormID():activeActor;
    if(id&&applied.depth>0.f&&!Raise(mount,applied))pending.insert_or_assign(id,applied);
    Remove(player);Remove(mount);depth=0.f;applied={};activeActor=0;
}
inline float Offset(){return depth;}
inline void Reset(){depth=0.f;applied={};activeActor=0;pending.clear();}
inline void Forget(RE::FormID id){pending.erase(id);if(activeActor==id)activeActor=0;}
inline void Recover(RE::Actor* actor){
    if(!actor)return;
    const auto it=pending.find(actor->GetFormID());
    if(it!=pending.end()&&Raise(actor,it->second))pending.erase(it);
}
inline void Save(SKSE::SerializationInterface* save,RE::Actor* mount){
    const auto current=mount?mount->GetFormID():activeActor;
    const auto write=[&](RE::FormID id,const Recovery& value){
        if(save->OpenRecord('WATR',2)){
            save->WriteRecordData(id);save->WriteRecordData(value.cell);
            save->WriteRecordData(value.lowered.x);save->WriteRecordData(value.lowered.y);
            save->WriteRecordData(value.lowered.z);save->WriteRecordData(value.depth);
        }
    };
    for(const auto& [id,value]:pending)if(id!=current)write(id,value);
    if(current&&applied.depth>0.f)write(current,applied);
}
inline bool Load(SKSE::SerializationInterface* save,std::uint32_t type,std::uint32_t version,std::uint32_t length){
    if(type!='WATR')return false;
    std::uint32_t id=0,resolved=0;Recovery value;
    if(version==1){
        // Old records contain no location: applying an additive lift after load
        // can raise an actor already settled by native physics. Do not replay it.
        spdlog::warn("Legacy WATR v1 has no position anchor; skipped unsafe additive height recovery");
        return true;
    }
    if(version==2&&length==24&&save->ReadRecordData(&id,4)==4&&save->ReadRecordData(&value.cell,4)==4&&
        save->ReadRecordData(&value.lowered.x,4)==4&&save->ReadRecordData(&value.lowered.y,4)==4&&
        save->ReadRecordData(&value.lowered.z,4)==4&&save->ReadRecordData(&value.depth,4)==4&&
        std::isfinite(value.lowered.x)&&std::isfinite(value.lowered.y)&&std::isfinite(value.lowered.z)&&
        std::isfinite(value.depth)&&value.depth>0&&save->ResolveFormID(id,resolved)&&
        save->ResolveFormID(value.cell,value.cell))pending.insert_or_assign(resolved,value);
    return true;
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
