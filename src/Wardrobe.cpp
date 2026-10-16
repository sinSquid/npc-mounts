#include "pch.h"
#include "Wardrobe.h"
#include <vector>
namespace Wardrobe {
void Undress(RE::Actor* actor){
    auto* equip=RE::ActorEquipManager::GetSingleton();if(!equip)return;
    // Clothing/armor regions; preserve hair/head adornments, jewelry, weapons,
    // shields, tails and face/FX helpers used by custom follower appearances.
    constexpr std::uint32_t clothing=(1u<<2)|(1u<<3)|(1u<<4)|(1u<<7)|(1u<<8)|
        (1u<<16)|(1u<<17)|(1u<<18)|(1u<<19)|(1u<<22)|(1u<<23)|(1u<<24)|
        (1u<<26)|(1u<<27)|(1u<<28)|(1u<<29)|(1u<<30);
    auto inventory=actor->GetInventory();
    std::vector<std::pair<RE::TESObjectARMO*,RE::ExtraDataList*>> worn;
    auto* cuffs=RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectARMO>(0x821,"NPCMounts.esp");
    for(const auto& [object,item]:inventory){
        auto* armor=object?object->As<RE::TESObjectARMO>():nullptr;const auto& entry=item.second;
        if(!armor||!entry||item.first<=0||!entry->IsWorn()||entry->IsQuestObject()||armor==actor->GetSkin()||armor==cuffs||
            (armor->GetFormFlags()&(1u<<2))||!(armor->GetSlotMask().underlying()&clothing))continue;
        RE::ExtraDataList* instance=nullptr;
        if(entry->extraLists)for(auto* extra:*entry->extraLists)if(extra&&(extra->HasType<RE::ExtraWorn>()||extra->HasType<RE::ExtraWornLeft>())){instance=extra;break;}
        worn.emplace_back(armor,instance);
    }
    // Unequip only: no AddItem/RemoveItem/container transfer or base outfit mutation.
    for(const auto& [armor,instance]:worn)equip->UnequipObject(actor,armor,instance,1,nullptr,false,false,false,true);
    spdlog::info("Clothing unequip requested ref={:08X} items={}",actor->GetFormID(),worn.size());
}
}
