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
struct Cell{unsigned id=10;unsigned GetFormID(){return id;}};
struct Actor{
    unsigned id=1;bool loaded=true,controllerReady=true,ownAbility=false,otherAbility=true;
    float water=0;NiPoint3 position;Cell cell;
    Cell* GetParentCell(){return &cell;}
    struct Root{struct{float scale=1;}world;}root;
    struct Controller{struct{hkpCharacterStateType currentState=hkpCharacterStateType::kOnGround;}context;}cc;
    bool HasSpell(SpellItem*){return ownAbility;}bool AddSpell(SpellItem*){ownAbility=true;return true;}bool RemoveSpell(SpellItem*){ownAbility=false;return true;}
    Root* Get3D(){return loaded?&root:nullptr;}Controller* GetCharController(){return controllerReady?&cc:nullptr;}
    float GetWaterHeight(){return water;}NiPoint3 GetPosition(){return position;}void SetPosition(NiPoint3 p,bool){position=p;}FormID GetFormID(){return id;}
};
}
namespace SKSE {
struct SerializationInterface {
 std::vector<char> bytes;std::vector<std::vector<char>> completed;std::size_t cursor=0;unsigned type=0,version=0;
 bool OpenRecord(unsigned t,unsigned v){if(!bytes.empty())completed.push_back(bytes);type=t;version=v;bytes.clear();cursor=0;return true;}
 template<class T>bool WriteRecordData(T value){auto* p=reinterpret_cast<const char*>(&value);bytes.insert(bytes.end(),p,p+sizeof(T));return true;}
 unsigned ReadRecordData(void* p,unsigned n){if(cursor+n>bytes.size())return 0;std::memcpy(p,bytes.data()+cursor,n);cursor+=n;return n;}
 bool ResolveFormID(unsigned id,unsigned& out){out=id;return true;}
};
}
namespace spdlog {inline void warn(const char*){}}
#include "../src/WaterRide.h"
namespace Crawl {bool Anchor(RE::Actor*,RE::NiPoint3& p,float){p={0,-18,45};return true;}}
void Apply(RE::Actor& actor){actor.position.z=-WaterRide::Offset();WaterRide::Applied(&actor);}
int main(){
 RE::SpellItem spell;WaterRide::ability=&spell;
 for(float fps:{30.f,60.f,144.f}){
  RE::Actor player,mount;WaterRide::Begin(&player,&mount);
  for(int i=0;i<int(fps*.5f);++i)WaterRide::Update(&player,&mount,1/fps);
  assert(std::abs(WaterRide::Offset()-15.f*(1-std::exp(-.5f/.15f)))<.0001f);
 }
 for(float scale:{.5f,1.f,1.7f,3.f}){
  RE::Actor player,mount;mount.id=2;mount.root.world.scale=scale;
  WaterRide::Begin(&player,&mount);assert(player.ownAbility&&mount.ownAbility);
  for(int i=0;i<120;++i)WaterRide::Update(&player,&mount,1.f/60);assert(std::abs(WaterRide::Offset()-15*scale)<.0001f);
  player.cc.context.currentState=RE::hkpCharacterStateType::kInAir;for(int i=0;i<120;++i)WaterRide::Update(&player,&mount,1.f/60);assert(WaterRide::Offset()==0);
  player.cc.context.currentState=RE::hkpCharacterStateType::kOnGround;player.position.z=100;WaterRide::Update(&player,&mount,.15f);assert(WaterRide::Offset()==0);
  player.position.z=0;player.water=-1e30f;WaterRide::Update(&player,&mount,.15f);assert(WaterRide::Offset()==0);
  player.water=0;WaterRide::Update(&player,&mount,.15f);Apply(mount);
  SKSE::SerializationInterface save;WaterRide::Save(&save,&mount);assert(save.type=='WATR'&&save.version==2&&save.bytes.size()==24);
  WaterRide::Reset();assert(WaterRide::Load(&save,'WATR',2,24));mount.loaded=false;WaterRide::Recover(&mount);assert(WaterRide::pending.contains(2));
  SKSE::SerializationInterface pending;WaterRide::Save(&pending,nullptr);assert(pending.bytes==save.bytes);WaterRide::Reset();assert(WaterRide::Load(&pending,'WATR',2,24));
  mount.loaded=true;WaterRide::Recover(&mount);assert(std::abs(mount.position.z)<.0001f&&WaterRide::pending.empty());WaterRide::Recover(&mount);assert(std::abs(mount.position.z)<.0001f);
  WaterRide::Update(&player,&mount,.15f);Apply(mount);WaterRide::End(&player,&mount);
  assert(!player.ownAbility&&!mount.ownAbility&&player.otherAbility&&mount.otherAbility&&WaterRide::Offset()==0&&std::abs(mount.position.z)<.0001f);
 }
 // Unloaded dismount followed by another mount/save must retain both recovery debts.
 {
  RE::Actor player,a,b;a.id=2;b.id=3;WaterRide::Reset();WaterRide::Begin(&player,&a);
  for(int i=0;i<120;++i)WaterRide::Update(&player,&a,1.f/60);
  Apply(a);a.loaded=false;WaterRide::Update(&player,&a,.1f);
  WaterRide::End(&player,&a);assert(WaterRide::pending.contains(2));
  WaterRide::Begin(&player,&b);for(int i=0;i<120;++i)WaterRide::Update(&player,&b,1.f/60);Apply(b);
  SKSE::SerializationInterface saved;WaterRide::Save(&saved,&b);assert(saved.completed.size()==1);
  saved.completed.push_back(saved.bytes);WaterRide::Reset();
  for(auto& bytes:saved.completed){SKSE::SerializationInterface record;record.bytes=bytes;assert(WaterRide::Load(&record,'WATR',2,24));}
  assert(WaterRide::pending.size()==2);a.loaded=true;WaterRide::Recover(&a);WaterRide::Recover(&b);
  assert(a.position.z==0&&b.position.z==0&&WaterRide::pending.empty());
  WaterRide::pending[4]={10,{},5};WaterRide::Forget(4);assert(WaterRide::pending.empty());
  WaterRide::Begin(&player,&a);WaterRide::Update(&player,&a,1.f);Apply(a);
  WaterRide::End(&player,nullptr);assert(WaterRide::pending.contains(2));
  WaterRide::Recover(&a);assert(a.position.z==0&&WaterRide::pending.empty());
  WaterRide::Begin(&player,&a);WaterRide::Update(&player,&a,1.f);const float applied=WaterRide::Offset();Apply(a);
  a.controllerReady=false;player.position.z=100;WaterRide::Update(&player,&a,.5f);assert(WaterRide::Offset()==applied);
  WaterRide::End(&player,&a);assert(a.position.z==0);
 }
 // The old implementation added depth to all of these already-moved positions.
 for(int scenario=0;scenario<6;++scenario){
  RE::Actor p,m;WaterRide::Reset();WaterRide::Begin(&p,&m);WaterRide::Update(&p,&m,1.f);Apply(m);
  float expected=0;
  if(scenario==0)m.position.z=0; // Native support already restored the surface.
  if(scenario==1)m.position.z=-WaterRide::Offset()/2; // Partially restored.
  if(scenario==2){m.position.z=30;expected=30;} // External vertical move.
  if(scenario==3){m.position.x=100;expected=m.position.z;} // External horizontal move.
  if(scenario==4){m.cell.id=20;expected=m.position.z;} // Same coordinates in another cell.
  if(scenario==5){m.position.z=-100;expected=-100;} // External downward move.
  WaterRide::End(&p,&m);assert(std::abs(m.position.z-expected)<.0001f);
  WaterRide::End(&p,&m);assert(std::abs(m.position.z-expected)<.0001f);
 }
 // A computed depth that never reached SyncMount creates no restoration debt.
 {RE::Actor p,m;WaterRide::Reset();WaterRide::Begin(&p,&m);WaterRide::Update(&p,&m,1.f);
  WaterRide::End(&p,&m);assert(m.position.z==0);}
 // Legacy v1 has no position anchor: recognize it without guessing an additive lift.
 {WaterRide::Reset();SKSE::SerializationInterface old;old.WriteRecordData(1u);old.WriteRecordData(15.f);
  assert(WaterRide::Load(&old,'WATR',1,8)&&WaterRide::pending.empty());}
 std::puts("PASS: applied-only anchored recovery; native full/partial support, external XYZ/cell changes, deferred multi-actor WATR v2, legacy no-blind-lift; ability/scale/depth lifecycle. Engine water physics untested.");
}
