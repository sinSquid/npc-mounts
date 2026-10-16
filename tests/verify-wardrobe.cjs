const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),s=fs.readFileSync(path.join(root,'src/Wardrobe.cpp'),'utf8');
const start=s.indexOf('void Undress(');let end=s.indexOf('{',start)+1,n=1;while(n){const c=s[end++];if(c==='{')n++;if(c==='}')n--;}
fs.writeFileSync(path.join(root,'build/wardrobe-regression.cpp'),`
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <cassert>
#include <cstdio>
namespace RE{
struct ExtraWorn{};struct ExtraWornLeft{};struct ExtraDataList{bool worn=false;template<class T>bool HasType(){return worn;}};
struct TESObjectARMO;struct TESBoundObject{bool armor=true;template<class T>T* As(){return armor?static_cast<T*>(this):nullptr;}};
struct TESObjectARMO:TESBoundObject{unsigned mask=1u<<2,flags=0;struct Mask{unsigned value;unsigned underlying(){return value;}};Mask GetSlotMask(){return{mask};}unsigned GetFormFlags(){return flags;}};
struct Entry{bool worn=true,quest=false;std::vector<ExtraDataList*>* extraLists=nullptr;bool IsWorn(){return worn;}bool IsQuestObject(){return quest;}};
using Inventory=std::map<TESBoundObject*,std::pair<int,std::shared_ptr<Entry>>>;
struct Actor{Inventory inventory;TESObjectARMO* skin=nullptr;auto GetInventory(){return inventory;}TESObjectARMO* GetSkin(){return skin;}unsigned GetFormID(){return 1;}};
TESObjectARMO cuff;struct TESDataHandler{static TESDataHandler* GetSingleton(){static TESDataHandler x;return &x;}template<class T>T* LookupForm(unsigned,const char*){return &cuff;}};
std::vector<std::pair<TESObjectARMO*,ExtraDataList*>>removed;
struct ActorEquipManager{static ActorEquipManager* GetSingleton(){static ActorEquipManager x;return &x;}bool UnequipObject(Actor* a,TESObjectARMO* item,ExtraDataList* extra,unsigned count,void*,bool queue,bool force,bool sounds,bool now){assert(count==1&&!queue&&!force&&!sounds&&now);removed.emplace_back(item,extra);a->inventory.at(item).second->worn=false;return true;}};
}
namespace spdlog{template<class...T>void info(const char*,T...){}}
${s.slice(start,end)}
int main(){RE::Actor a;RE::TESObjectARMO body,boots,skin,hair,ring,quest,hidden,weapon,loose,missing;
 skin.mask=body.mask;hair.mask=1u<<1;ring.mask=1u<<6;boots.mask=1u<<7;hidden.flags=4;weapon.armor=false;a.skin=&skin;
 for(auto* item:{&body,&boots,&skin,&hair,&ring,&quest,&hidden,&weapon,&loose,&RE::cuff})a.inventory[item]={3,std::make_shared<RE::Entry>()};a.inventory[&quest].second->quest=true;a.inventory[&loose].second->worn=false;a.inventory[&missing]={1,nullptr};
 RE::ExtraDataList spare{false},instance{true};std::vector<RE::ExtraDataList*> extras{&spare,&instance};a.inventory[&body].second->extraLists=&extras;
 Undress(&a);assert(RE::removed.size()==2);for(auto [item,extra]:RE::removed){assert(item==&body||item==&boots);if(item==&body)assert(extra==&instance);}
 for(auto* item:{&body,&boots,&skin,&hair,&ring,&quest,&hidden,&weapon,&loose,&RE::cuff})assert(a.inventory.at(item).first==3);
 Undress(&a);assert(RE::removed.size()==2); // No extra item and no repeated unequip of already removed clothing.
 for(auto* item:{&skin,&hair,&ring,&quest,&hidden,&weapon,&RE::cuff})assert(a.inventory.at(item).second->worn);
 std::puts("PASS: production clothing unequip preserves counts and worn instance; ignores skin/head jewelry/weapons/quest/nonplayable/cuffs/unworn/null entries; repeat is idempotent.");}
`);
