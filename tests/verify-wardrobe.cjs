const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/Wardrobe.cpp'),'utf8');
assert(source.includes('void Capture(RE::Actor* actor,bool clothing)'));
assert(source.indexOf('snapshot.drawn=actor->AsActorState()->IsWeaponDrawn()')<source.indexOf('for(bool left:{true,false})'),'drawn state must be captured before hands are unequipped');
assert(source.includes('GetEquippedObject(left)')&&source.includes('selectedSpells[')&&source.includes('InterruptCast(false)'));
assert(source.includes('if(accepted)snapshot.clothing.push_back(saved)')&&source.includes('ExtraUniqueID'));
const restore=source.slice(source.indexOf('void Restore('));
assert(restore.indexOf('if(!equip)return')<restore.indexOf('snapshots.erase(it)'));
assert(source.includes('void Restore(RE::Actor* actor)')&&source.includes('EquipSpell(actor,spell,slot)')&&source.includes('EquipObject(actor,armor,Instance(inventory,item)'));
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
namespace RE {
struct ExtraUniqueID{unsigned uniqueID=42;};
struct ExtraDataList{bool valid=true;ExtraUniqueID id;template<class T>T* GetByType(){assert(valid);return &id;}};
struct TESObjectARMO{unsigned GetFormID(){return 77;}};
}
struct Actor{unsigned GetFormID(){return 1;}};
struct Equip{bool accepted=true;bool UnequipObject(Actor*,RE::TESObjectARMO*,RE::ExtraDataList* extra,int,void*,bool,bool,bool,bool){extra->valid=false;return accepted;}};
struct Item{unsigned form,unique=0xFFFFFFFF;};
void run(bool accepted){
 Actor owner;Actor* actor=&owner;Equip manager;manager.accepted=accepted;auto* equip=&manager;
 RE::TESObjectARMO armor;RE::ExtraDataList extra;
 std::vector<std::pair<RE::TESObjectARMO*,RE::ExtraDataList*>> worn{{&armor,&extra}};
 struct{std::vector<Item> clothing;}snapshot;
 ${source.slice(start,end)}
 assert(snapshot.clothing.size()==(accepted?1:0));
 if(accepted)assert(snapshot.clothing[0].unique==42);
}
int main(){run(true);run(false);std::puts("PASS: production unequip loop captures instance identity before invalidation; rejected requests do not enter restore ledger. Restore singleton ordering also checked.");}
`);
