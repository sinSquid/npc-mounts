const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/Wardrobe.cpp'),'utf8');
assert(source.includes('void Capture(RE::Actor* actor,bool clothing)'));
assert(source.indexOf('snapshot.drawn=actor->AsActorState()->IsWeaponDrawn()')<source.indexOf('for(bool left:{true,false})'),'drawn state must be captured before hands are unequipped');
assert(source.includes('GetEquippedObject(left)')&&source.includes('selectedSpells[')&&source.includes('InterruptCast(false)'));
assert(source.includes('if(accepted)snapshot.clothing.push_back(saved)')&&source.includes('ExtraUniqueID'));
const restore=source.slice(source.indexOf('void Restore('));
assert(restore.indexOf('if(!equip)return')<restore.indexOf('snapshots.erase(it)'));
assert(source.includes('void Restore(RE::Actor* actor)')&&source.includes('EquipSpell(actor,spell,slot)')&&source.includes('EquipObject(actor,armor,instance'));
assert(source.includes('const auto inventory=saved.clothing.empty()?Inventory{}:actor->GetInventory()'));
assert(source.includes('if(!worn.empty()&&spdlog::default_logger()->should_log(spdlog::level::info))'));
assert(source.includes("OpenRecord('GEAR',1)")&&source.includes("if(type!='GEAR')"));
assert(!source.includes('AddItem(')&&!source.includes('RemoveItem('));
const start=source.indexOf('    for(const auto& [armor,instance]:worn){'),end=source.indexOf('\n    if(!worn.empty()',start);
assert(start>=0&&end>start);
fs.writeFileSync(path.join(repo,'build/wardrobe-regression.cpp'),`
#include <cassert>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <map>
namespace Diagnostics{template<class... A>void Trace(A&&...){}}
namespace RE {
struct ExtraUniqueID{unsigned uniqueID=42;};
struct ExtraDataList{bool valid=true;ExtraUniqueID id;template<class T>T* GetByType(){assert(valid);return &id;}};
struct TESBoundObject{};
struct TESObjectARMO:TESBoundObject{unsigned GetFormID(){return 77;}};
struct TESForm{template<class T> static T* LookupByID(unsigned id){static T armor;return id==77?&armor:nullptr;}};
}
struct Actor{unsigned GetFormID(){return 1;}};
struct Equip{bool accepted=true;bool UnequipObject(Actor*,RE::TESObjectARMO*,RE::ExtraDataList* extra,int,void*,bool,bool,bool,bool){extra->valid=false;return accepted;}
 std::vector<RE::ExtraDataList*> equipped;
 void EquipObject(Actor*,RE::TESObjectARMO*,RE::ExtraDataList* extra,int,void*,bool,bool,bool,bool){equipped.push_back(extra);}
};
struct Item{unsigned form,unique=0xFFFFFFFF;};
struct Entry{std::vector<RE::ExtraDataList*>* extraLists;};
using Inventory=std::map<RE::TESBoundObject*,std::pair<int,Entry*>>;
${source.slice(source.indexOf('RE::ExtraDataList* Instance('),source.indexOf('\n}\n}',source.indexOf('RE::ExtraDataList* Instance('))+2)}
void restoreClothing(const Inventory& inventory,Item item,Equip* equip){
 Actor owner;Actor* actor=&owner;
 struct{std::vector<Item> clothing;}saved{{item}};
 ${restore.slice(restore.indexOf('    for(const auto& item:saved.clothing)'),restore.indexOf('    const auto equipHand'))}
}
void checkRestore(){
 auto* armor=RE::TESForm::LookupByID<RE::TESObjectARMO>(77);
 RE::ExtraDataList original,other;other.id.uniqueID=7;
 std::vector<RE::ExtraDataList*> extras{&other};Entry entry{&extras};Inventory inventory{{armor,{1,&entry}}};
 Equip equip;
 restoreClothing(inventory,{77,42},&equip);assert(equip.equipped.empty());
 extras.push_back(&original);inventory[armor].first=2;
 restoreClothing(inventory,{77,42},&equip);assert(equip.equipped.size()==1&&equip.equipped.back()==&original);
 inventory[armor].first=0;restoreClothing(inventory,{77},&equip);assert(equip.equipped.size()==1);
 inventory[armor]={1,nullptr};restoreClothing(inventory,{77},&equip);assert(equip.equipped.size()==2&&!equip.equipped.back());
 inventory.clear();restoreClothing(inventory,{77},&equip);assert(equip.equipped.size()==2);
}
void run(bool accepted){
 Actor owner;Actor* actor=&owner;Equip manager;manager.accepted=accepted;auto* equip=&manager;
 RE::TESObjectARMO armor;RE::ExtraDataList extra;
 std::vector<std::pair<RE::TESObjectARMO*,RE::ExtraDataList*>> worn{{&armor,&extra}};
 struct{std::vector<Item> clothing;}snapshot;
 ${source.slice(start,end)}
 assert(snapshot.clothing.size()==(accepted?1:0));
 if(accepted)assert(snapshot.clothing[0].unique==42);
}
int main(){run(true);run(false);checkRestore();std::puts("PASS: equipment capture and restore preserve available instance IDs, skip missing instances and support untagged armor.");}
`);
