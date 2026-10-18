const fs=require('fs'),assert=require('assert');
function take(file,sig){const s=fs.readFileSync(file,'utf8'),start=s.indexOf(sig);assert(start>=0,sig);let end=s.indexOf('{',start)+1,n=1;while(n){const c=s[end++];if(c==='{')n++;if(c==='}')n--;}return s.slice(start,end);}
fs.writeFileSync('build/spell-migration-regression.cpp',`
#include <array>
#include <set>
#include <unordered_map>
#include <cassert>
#include <cstdint>
#include <cstdio>
namespace RE{
using FormID=unsigned;
template<class T>struct NiPointer{T* value=nullptr;NiPointer()=default;NiPointer(T* p):value(p){}T* get(){return value;}T* operator->(){return value;}explicit operator bool()const{return value;}};
struct TESObjectREFR{template<class T>T* As(){return static_cast<T*>(this);}};
struct SpellItem{unsigned id;};
struct PlayerCharacter{std::set<SpellItem*> learned;int adds=0,removes=0;bool addOK=true;
 bool HasSpell(SpellItem* s){return learned.contains(s);}bool AddSpell(SpellItem* s){++adds;if(!addOK)return false;learned.insert(s);return true;}void RemoveSpell(SpellItem* s){++removes;learned.erase(s);}};
struct TESDataHandler{std::unordered_map<unsigned,SpellItem*> spells;static TESDataHandler* GetSingleton(){static TESDataHandler h;return &h;}template<class T>T* LookupForm(unsigned id,const char*){auto i=spells.find(id);return i==spells.end()?nullptr:i->second;}};
struct Actor:TESObjectREFR{bool player=false;unsigned GetFormID(){return 1;}bool IsPlayerRef(){return player;}};struct EffectSetting{unsigned id;unsigned GetFormID(){return id;}};
struct CrosshairPickData{NiPointer<TESObjectREFR> target;static CrosshairPickData* GetSingleton(){static CrosshairPickData p;return &p;}NiPointer<TESObjectREFR> GetActiveTarget(){return target;}};
}
constexpr auto plugin="NPCMounts.esp";RE::SpellItem* convertSpell=nullptr;
${take('src/plugin.cpp','void MigrateKnownSpells(')}
namespace Extras{
std::array<RE::EffectSetting*,4>effects;int anchors=0,captures=0,leashes=0,notices=0;
void Bind(RE::Actor*,bool capture){capture?++captures:++anchors;}void ToggleTether(RE::Actor*,bool){++leashes;}void Notify(const char*){++notices;}
${take('src/Extras.cpp','bool Handle(')}
}
int main(){
 RE::SpellItem normal{0x804},sex{0x823},capture{0x80B},toggle{0x80C},unleash{0x80D};convertSpell=&normal;
 RE::TESDataHandler::GetSingleton()->spells={{sex.id,&sex},{capture.id,&capture},{toggle.id,&toggle},{unleash.id,&unleash}};
 RE::PlayerCharacter p;MigrateKnownSpells(&p);assert(p.learned.empty()&&p.adds==0&&p.removes==0);
 p.learned={&sex,&capture,&unleash};MigrateKnownSpells(&p);assert(p.learned==std::set<RE::SpellItem*>({&normal,&capture,&toggle})&&p.adds==2&&p.removes==2);
 MigrateKnownSpells(&p);assert(p.adds==2&&p.removes==2);
 RE::PlayerCharacter failure;failure.addOK=false;failure.learned={&sex,&capture};MigrateKnownSpells(&failure);assert(failure.HasSpell(&sex)&&failure.HasSpell(&capture));
 RE::EffectSetting a{0x806},captureEffect{0x807},e{0x808},f{0x809};Extras::effects={&a,&captureEffect,&e,&f};RE::Actor actor;
 assert(Extras::Handle(captureEffect.id,&actor)&&Extras::captures==1&&Extras::anchors==0&&Extras::leashes==0);
 assert(Extras::Handle(a.id,&actor)&&Extras::anchors==1);assert(Extras::Handle(e.id,&actor)&&Extras::leashes==1);
 puts("PASS: actual spell migration does not auto-learn, merges old compatibility spells once, preserves capture, and routes its effect to capture. Not game QA.");
}
`);
