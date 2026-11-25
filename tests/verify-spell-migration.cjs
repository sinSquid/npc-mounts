const fs=require('fs'),assert=require('assert');
function take(file,sig){const s=fs.readFileSync(file,'utf8'),start=s.indexOf(sig);assert(start>=0,sig);let end=s.indexOf('{',start)+1,n=1;while(n){const c=s[end++];if(c==='{')n++;if(c==='}')n--;}return s.slice(start,end);}
// Exercise Message's ordering and conditions. Replace engine installation and
// save recovery with counters; preserve both early returns and the ready flag.
const message=take('src/plugin.cpp','void Message(');
function replaceRegion(code,first,after,replacement){
 const begin=code.indexOf(first),end=code.indexOf(after,begin);
 assert(begin>=0&&end>begin,first);
 return code.slice(0,begin)+replacement+code.slice(end);
}
let lifecycle=replaceRegion(message,'auto* data =','if (!mountFaction','');
lifecycle=replaceRegion(lifecycle,'Extras::Initialize();','RegisterRideInput();','');
lifecycle=replaceRegion(lifecycle,'        spdlog::info("Ready','\n    }','');
lifecycle=replaceRegion(lifecycle,'auto* player =','\n    }','++recoveries;');
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
namespace Lifecycle{
namespace SKSE{struct MessagingInterface{enum{kDataLoaded,kNewGame,kPostLoadGame};struct Message{int type;void* data;};};}
namespace spdlog{template<class... T>void error(const char*,T...){}}
namespace PoseOutput{bool available=true;int installs=0;bool Install(){++installs;return available;}}
bool gameplayInitialized=false,mountFaction=true,riderFaction=true,convertEffect=true,restoreEffect=true,convertSpell=true,restoreSpell=true;
int installs=0,recoveries=0;void RegisterRideInput(){++installs;}
${lifecycle}
void Test(){
 using M=SKSE::MessagingInterface;
 M::Message data{M::kDataLoaded,nullptr},fresh{M::kNewGame,nullptr},loaded{M::kPostLoadGame,&data},failed{M::kPostLoadGame,nullptr};
 for(auto* form:{&mountFaction,&riderFaction,&convertEffect,&restoreEffect,&convertSpell,&restoreSpell}){
  *form=false;Message(&data);Message(&fresh);Message(&loaded);assert(PoseOutput::installs==0&&installs==0&&recoveries==0);*form=true;
 }
 PoseOutput::available=false;Message(&data);Message(&loaded);assert(!gameplayInitialized&&recoveries==0);
 PoseOutput::available=true;Message(&data);Message(&data);Message(&failed);assert(installs==1&&recoveries==0);
 Message(&fresh);Message(&loaded);assert(recoveries==2);
}
}
namespace Extras{
std::array<RE::EffectSetting*,4>effects;int anchors=0,captures=0,leashes=0,notices=0;
void Bind(RE::Actor*,bool capture){capture?++captures:++anchors;}void ToggleTether(RE::Actor*,bool){++leashes;}void Notify(const char*){++notices;}
${take('src/Extras.cpp','bool Handle(')}
}
int main(){
 Lifecycle::Test();
 RE::SpellItem normal{0x804},sex{0x823},capture{0x80B},toggle{0x80C},unleash{0x80D};convertSpell=&normal;
 RE::TESDataHandler::GetSingleton()->spells={{sex.id,&sex},{capture.id,&capture},{toggle.id,&toggle},{unleash.id,&unleash}};
 RE::PlayerCharacter p;MigrateKnownSpells(&p);assert(p.learned.empty()&&p.adds==0&&p.removes==0);
 p.learned={&sex,&capture,&unleash};MigrateKnownSpells(&p);assert(p.learned==std::set<RE::SpellItem*>({&normal,&capture,&toggle})&&p.adds==2&&p.removes==2);
 MigrateKnownSpells(&p);assert(p.adds==2&&p.removes==2);
 RE::PlayerCharacter failure;failure.addOK=false;failure.learned={&sex,&capture};MigrateKnownSpells(&failure);assert(failure.HasSpell(&sex)&&failure.HasSpell(&capture));
 RE::EffectSetting a{0x806},captureEffect{0x807},e{0x808},f{0x809};Extras::effects={&a,&captureEffect,&e,&f};RE::Actor actor;
 assert(Extras::Handle(captureEffect.id,&actor)&&Extras::captures==1&&Extras::anchors==0&&Extras::leashes==0);
 assert(Extras::Handle(a.id,&actor)&&Extras::anchors==1);assert(Extras::Handle(e.id,&actor)&&Extras::leashes==1);
 puts("PASS: incomplete forms or unavailable pose hook disable session recovery, successful initialization runs once, failed loads skip recovery; spell migration and capture routing passed.");
}
`);
