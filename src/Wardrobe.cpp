#include "pch.h"
#include "Wardrobe.h"
#include "Diagnostics.h"
#include <mutex>
#include <unordered_map>
#include <vector>
namespace Wardrobe {
namespace {
struct Item {RE::FormID form{};std::uint32_t unique=0xFFFFFFFF;};
struct Snapshot {RE::FormID left{},right{};bool drawn{};std::vector<Item> clothing;};
std::unordered_map<RE::FormID,Snapshot> snapshots;
std::mutex mutex;
using Inventory=decltype(std::declval<RE::Actor*>()->GetInventory());
RE::ExtraDataList* Instance(const Inventory& inventory,const Item& saved){
    auto* object=RE::TESForm::LookupByID<RE::TESBoundObject>(saved.form);if(!object)return nullptr;
    auto it=inventory.find(object);if(it==inventory.end()||!it->second.second||!it->second.second->extraLists)return nullptr;
    RE::ExtraDataList* fallback=nullptr;
    for(auto* extra:*it->second.second->extraLists)if(extra){
        if(!fallback)fallback=extra;
        if(saved.unique!=0xFFFFFFFF)if(auto* id=extra->GetByType<RE::ExtraUniqueID>();id&&id->uniqueID==saved.unique)return extra;
    }
    return saved.unique==0xFFFFFFFF?fallback:nullptr;
}
}
void Capture(RE::Actor* actor,bool clothing){
    auto* equip=RE::ActorEquipManager::GetSingleton();if(!equip)return;
    Snapshot snapshot;
    snapshot.drawn=actor->AsActorState()->IsWeaponDrawn();
    for(bool left:{true,false})if(auto* form=actor->GetEquippedObject(left)){
        (left?snapshot.left:snapshot.right)=form->GetFormID();
        auto* slot=RE::TESForm::LookupByID<RE::BGSEquipSlot>(left?0x13F43:0x13F42);
        if(auto* object=form->As<RE::TESBoundObject>())equip->UnequipObject(actor,object,nullptr,1,slot,false,false,false,true);
        else actor->GetActorRuntimeData().selectedSpells[left?RE::Actor::SlotTypes::kLeftHand:RE::Actor::SlotTypes::kRightHand]=nullptr;
    }
    actor->InterruptCast(false);actor->DrawWeaponMagicHands(false);
    if(!clothing){std::scoped_lock lock(mutex);snapshots.insert_or_assign(actor->GetFormID(),std::move(snapshot));return;}
    // Clothing/armor regions; preserve hair/head adornments, jewelry, weapons,
    // shields, tails and face/FX helpers used by custom follower appearances.
    constexpr std::uint32_t clothingSlots=(1u<<2)|(1u<<3)|(1u<<4)|(1u<<7)|(1u<<8)|
        (1u<<16)|(1u<<17)|(1u<<18)|(1u<<19)|(1u<<22)|(1u<<23)|(1u<<24)|
        (1u<<26)|(1u<<27)|(1u<<28)|(1u<<29)|(1u<<30);
    auto inventory=actor->GetInventory();
    std::vector<std::pair<RE::TESObjectARMO*,RE::ExtraDataList*>> worn;
    auto* cuffs=RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectARMO>(0x821,"NPCMounts.esp");
    for(const auto& [object,item]:inventory){
        auto* armor=object?object->As<RE::TESObjectARMO>():nullptr;const auto& entry=item.second;
        if(!armor||!entry||item.first<=0||!entry->IsWorn()||entry->IsQuestObject()||armor==actor->GetSkin()||armor==cuffs||
            (armor->GetFormFlags()&(1u<<2))||!(armor->GetSlotMask().underlying()&clothingSlots))continue;
        RE::ExtraDataList* instance=nullptr;
        if(entry->extraLists)for(auto* extra:*entry->extraLists)if(extra&&(extra->HasType<RE::ExtraWorn>()||extra->HasType<RE::ExtraWornLeft>())){instance=extra;break;}
        worn.emplace_back(armor,instance);
    }
    // Unequip only: no AddItem/RemoveItem/container transfer or base outfit mutation.
    for(const auto& [armor,instance]:worn){
        const bool accepted=equip->UnequipObject(actor,armor,instance,1,nullptr,false,false,false,true);
        Diagnostics::Trace("Unequip request target={:08X} item={:08X} accepted={}",actor->GetFormID(),armor->GetFormID(),accepted);
        if(accepted){Item saved{armor->GetFormID()};if(instance)if(auto* id=instance->GetByType<RE::ExtraUniqueID>())saved.unique=id->uniqueID;snapshot.clothing.push_back(saved);}
    }
    if(!worn.empty()&&spdlog::default_logger()->should_log(spdlog::level::info)){
        const auto after=actor->GetInventory();
        for(const auto& [armor,instance]:worn){
            const auto old=inventory.find(armor);const auto now=after.find(armor);
            const int beforeCount=old!=inventory.end()?old->second.first:0;
            const int afterCount=now!=after.end()?now->second.first:0;
            const bool stillWorn=now!=after.end()&&now->second.second&&now->second.second->IsWorn();
            Diagnostics::Trace("Unequip observed target={:08X} item={:08X} count={}->{} stillWorn={} (immediate snapshot)",actor->GetFormID(),armor->GetFormID(),beforeCount,afterCount,stillWorn);
        }
    }
    spdlog::info("Clothing unequip requested ref={:08X} items={}",actor->GetFormID(),snapshot.clothing.size());
    std::scoped_lock lock(mutex);snapshots.insert_or_assign(actor->GetFormID(),std::move(snapshot));
}
void Restore(RE::Actor* actor){
    Snapshot saved;{std::scoped_lock lock(mutex);auto it=snapshots.find(actor->GetFormID());if(it==snapshots.end())return;saved=std::move(it->second);snapshots.erase(it);}
    auto* equip=RE::ActorEquipManager::GetSingleton();if(!equip)return;
    const auto inventory=saved.clothing.empty()?Inventory{}:actor->GetInventory();
    for(const auto& item:saved.clothing)if(auto* armor=RE::TESForm::LookupByID<RE::TESObjectARMO>(item.form))equip->EquipObject(actor,armor,Instance(inventory,item),1,nullptr,false,false,false,true);
    const auto equipHand=[&](RE::FormID id,bool left){
        auto* form=RE::TESForm::LookupByID(id);if(!form)return;
        auto* slot=RE::TESForm::LookupByID<RE::BGSEquipSlot>(left?0x13F43:0x13F42);
        if(auto* spell=form->As<RE::SpellItem>())equip->EquipSpell(actor,spell,slot);
        else if(auto* object=form->As<RE::TESBoundObject>())equip->EquipObject(actor,object,nullptr,1,slot,false,false,false,true);
    };
    equipHand(saved.left,true);equipHand(saved.right,false);if(saved.drawn)actor->DrawWeaponMagicHands(true);
    spdlog::info("Equipment restored ref={:08X} clothing={}",actor->GetFormID(),saved.clothing.size());
}
void Forget(RE::FormID id){std::scoped_lock lock(mutex);snapshots.erase(id);}
void Clear(){std::scoped_lock lock(mutex);snapshots.clear();}
void Save(SKSE::SerializationInterface* save){
    std::scoped_lock lock(mutex);save->OpenRecord('GEAR',1);save->WriteRecordData(static_cast<std::uint32_t>(snapshots.size()));
    for(const auto& [id,s]:snapshots){for(auto value:{id,s.left,s.right,std::uint32_t(s.drawn),static_cast<std::uint32_t>(s.clothing.size())})save->WriteRecordData(value);for(const auto& item:s.clothing){save->WriteRecordData(item.form);save->WriteRecordData(item.unique);}}
}
bool Load(SKSE::SerializationInterface* save,std::uint32_t type,std::uint32_t version,std::uint32_t length){
    if(type!='GEAR')return false;if(version!=1||length<4)return true;std::uint32_t count=0,used=4;if(save->ReadRecordData(&count,4)!=4)return true;
    for(unsigned i=0;i<count&&used+20<=length;++i){std::array<std::uint32_t,5> v{};if(save->ReadRecordData(v.data(),20)!=20)break;used+=20;Snapshot s;s.drawn=v[3]!=0;
        RE::FormID actor;if(!save->ResolveFormID(v[0],actor)){for(unsigned n=0;n<v[4]&&used+8<=length;++n){std::uint32_t skip[2];save->ReadRecordData(skip,8);used+=8;}continue;}
        save->ResolveFormID(v[1],s.left);save->ResolveFormID(v[2],s.right);
        for(unsigned n=0;n<v[4]&&used+8<=length;++n){std::uint32_t form=0,unique=0;if(save->ReadRecordData(&form,4)!=4||save->ReadRecordData(&unique,4)!=4)break;used+=8;Item item;item.unique=unique;if(save->ResolveFormID(form,item.form))s.clothing.push_back(item);}
        std::scoped_lock lock(mutex);snapshots[actor]=std::move(s);
    }return true;
}
}
