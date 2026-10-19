#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>
namespace RE {
using FormID=std::uint32_t;
struct SpellItem{};struct hkbGenerator;struct hkbContext;struct hkbGeneratorOutput;
struct NiPoint3{float x=0,y=0,z=0;};
enum class hkpCharacterStateType{kOnGround,kInAir,kJumping,kSwimming};
struct Actor{
    unsigned id=1;bool loaded=true,ownAbility=false,otherAbility=true;
    float water=0;NiPoint3 position;
    struct Root{struct{float scale=1;}world;}root;
    struct Controller{struct{hkpCharacterStateType currentState=hkpCharacterStateType::kOnGround;}context;}cc;
    bool HasSpell(SpellItem*){return ownAbility;}bool AddSpell(SpellItem*){ownAbility=true;return true;}bool RemoveSpell(SpellItem*){ownAbility=false;return true;}
    Root* Get3D(){return loaded?&root:nullptr;}Controller* GetCharController(){return &cc;}
    float GetWaterHeight(){return water;}NiPoint3 GetPosition(){return position;}void SetPosition(NiPoint3 p,bool){position=p;}FormID GetFormID(){return id;}
};
}
namespace SKSE {
struct SerializationInterface {
 std::vector<char> bytes;std::size_t cursor=0;unsigned type=0,version=0;
 bool OpenRecord(unsigned t,unsigned v){type=t;version=v;bytes.clear();cursor=0;return true;}
 template<class T>bool WriteRecordData(T value){auto* p=reinterpret_cast<const char*>(&value);bytes.insert(bytes.end(),p,p+sizeof(T));return true;}
 unsigned ReadRecordData(void* p,unsigned n){if(cursor+n>bytes.size())return 0;std::memcpy(p,bytes.data()+cursor,n);cursor+=n;return n;}
 bool ResolveFormID(unsigned id,unsigned& out){out=id;return true;}
};
}
#include "../src/WaterRide.h"
namespace Crawl {bool Anchor(RE::Actor*,RE::NiPoint3& p,float){p={0,-18,45};return true;}}
int main(){
 RE::SpellItem spell;WaterRide::ability=&spell;
 for(float scale:{.5f,1.f,1.7f,3.f}){
  RE::Actor player,mount;mount.id=2;mount.root.world.scale=scale;
  WaterRide::Begin(&player,&mount);assert(player.ownAbility&&mount.ownAbility);
  WaterRide::Update(&player,&mount,.15f);assert(std::abs(WaterRide::Offset()-15*scale)<.0001f);
  player.cc.context.currentState=RE::hkpCharacterStateType::kInAir;WaterRide::Update(&player,&mount,.15f);assert(WaterRide::Offset()==0);
  player.cc.context.currentState=RE::hkpCharacterStateType::kOnGround;player.position.z=100;WaterRide::Update(&player,&mount,.15f);assert(WaterRide::Offset()==0);
  player.position.z=0;player.water=-1e30f;WaterRide::Update(&player,&mount,.15f);assert(WaterRide::Offset()==0);
  player.water=0;WaterRide::Update(&player,&mount,.15f);mount.position.z=-WaterRide::Offset();
  SKSE::SerializationInterface save;WaterRide::Save(&save,&mount);assert(save.type=='WATR'&&save.version==1&&save.bytes.size()==8);
  WaterRide::Reset();assert(WaterRide::Load(&save,'WATR',1,8));mount.loaded=false;WaterRide::Recover(&mount);assert(WaterRide::savedActor==2);
  SKSE::SerializationInterface pending;WaterRide::Save(&pending,nullptr);assert(pending.bytes==save.bytes);WaterRide::Reset();assert(WaterRide::Load(&pending,'WATR',1,8));
  mount.loaded=true;WaterRide::Recover(&mount);assert(std::abs(mount.position.z)<.0001f&&WaterRide::savedActor==0);WaterRide::Recover(&mount);assert(std::abs(mount.position.z)<.0001f);
  WaterRide::Update(&player,&mount,.15f);mount.position.z=-WaterRide::Offset();WaterRide::End(&player,&mount);
  assert(!player.ownAbility&&!mount.ownAbility&&player.otherAbility&&mount.otherAbility&&WaterRide::Offset()==0&&std::abs(mount.position.z)<.0001f);
 }
 std::puts("PASS: production water ability lifetime, scaled one-third crawl height, air/land/no-water exit, own-spell-only removal, WATR delayed load recovery and no double raise. Engine water physics untested.");
}
