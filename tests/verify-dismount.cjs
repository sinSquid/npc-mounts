// Execute the real Dismount body with failed floor/wall queries, not a mock Dismount.
const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/plugin.cpp'),'utf8');
const start=source.indexOf('RE::NiPoint3 DismountOffset(');assert(start>=0);
let end=source.indexOf('{',source.indexOf('bool Dismount('))+1,depth=1;
while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}
const beginStart=source.indexOf('void BeginDismount() {',end);
const beginEnd=source.indexOf('bool SeatOutputTimedOut(',beginStart);
assert(beginStart>=0&&beginEnd>beginStart);
const code=`
#include <cassert>
#include "../src/Motion.h"
constexpr float transitionSideOffset=128.f;
Motion::RideState rideMotion;
namespace RideCollision {void Clear(){}}
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include "../src/RideControl.h"
namespace RE {
struct NiPoint3 {float x{},y{},z{};NiPoint3 operator+(NiPoint3 b)const{return{x+b.x,y+b.y,z+b.z};}NiPoint3 operator-(NiPoint3 b)const{return{x-b.x,y-b.y,z-b.z};}bool operator==(const NiPoint3&)const=default;};
struct hkVector4 {hkVector4(float,float,float,float){}};
struct bhkWorld {};
struct bhkPickData {struct {struct {float Dot3(hkVector4){return 1.f;}}normal;float hitFraction=0.5f;}rayOutput;};
struct Controller {bhkWorld world;auto* GetHavokWorld(){return &world;}};
struct Actor {enum class BOOL_FLAGS:unsigned{};struct Flags{unsigned bits=0;unsigned underlying()const{return bits;}void reset(unsigned v){bits&=~v;}void set(BOOL_FLAGS v){bits|=static_cast<unsigned>(v);}};struct{Flags boolFlags;}data;
 auto& GetActorRuntimeData(){return data;}
 int id=1,rank=3;bool marker=true,hasController=true;unsigned resetEvents=0,positionWrites=0;NiPoint3 pos{100,200,300};Controller cc;
 auto GetPosition(){return pos;}void SetPosition(NiPoint3 p,bool){pos=p;++positionWrites;}float GetAngleZ(){return 0.f;}float GetHeight(){return 128.f;}
 Controller* GetCharController(){return hasController?&cc:nullptr;}int GetFormID(){return id;}
 void RemoveFromFaction(void*){marker=false;}void AddToFaction(void*,int r){rank=r;}
 void NotifyAnimationGraph(const char*){++resetEvents;}
};
struct PlayerCharacter:Actor {static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;}};
struct PlayerCamera {static PlayerCamera* GetSingleton(){static PlayerCamera c;return &c;}void ForceFirstPerson(){}};
}
struct Handle {std::shared_ptr<RE::Actor> value;bool stale=false;auto get(){return value;}void reset(){value.reset();stale=false;}explicit operator bool()const{return value||stale;}}ridden;
struct State{int animation=3;};std::unordered_map<int,State> targets;
void *riderFaction{},*mountFaction{};int ridePhysics=1,proxyPhysics=2;unsigned playerControllerFlags{},mountControllerFlags{};
bool firstPersonBefore=false,wasAir=false;float jumpTimer{},transitionTime{};
float playerSpeedDelta=0;bool downActive=false,upActive=false;unsigned downFlags=7,downOriginalFlags=0;RE::NiPoint3 downOffset;HoldGate dismountHold;
int config=0;
namespace Seat{RE::NiPoint3 offset;void Entering(bool){}void Update(RE::Actor*,RE::Actor*,int,float,RE::NiPoint3 p){offset=p;}void Clear(){}}
namespace MovementCompat{void End(){}}
namespace RideDrive{void End(){}void ResetInput(){}}void ApplySpeed(RE::Actor*,float,float& delta){delta=0;}
int restores=0;void RestoreController(RE::Actor*,int,unsigned){++restores;}void StopMotion(RE::Actor*){}
bool floorFound=false,wallBlocked=false,rightBlocked=false,lowCeiling=false,shoulderBlocked=false;int rays=0;float floorZ=300;
bool Ray(RE::bhkWorld*,RE::NiPoint3 from,RE::NiPoint3 to,RE::bhkPickData& pick){
 ++rays;if(from.x==to.x&&from.y==to.y){
  if(from.z>to.z){pick.rayOutput.hitFraction=(from.z-floorZ)/(from.z-to.z);return floorFound;}
  return (lowCeiling||shoulderBlocked&&std::abs(from.x-228)>10.f)&&from.z<405.f&&to.z>405.f;
 }
 return wallBlocked||rightBlocked&&from.x==100.f&&to.x>from.x;
}
namespace spdlog{void info(const char*){}}
${source.slice(start,end)}
${source.slice(beginStart,beginEnd)}
int main(){
 for(int scenario=0;scenario<11;scenario++){
  auto mount=std::make_shared<RE::Actor>();ridden.value=mount;targets[1]={};
  auto* p=RE::PlayerCharacter::GetSingleton();p->pos={100,200,300};p->marker=true;p->hasController=scenario!=2;
  floorFound=scenario!=0;wallBlocked=scenario==1;rightBlocked=scenario==4;lowCeiling=scenario==7;shoulderBlocked=scenario==8;
  floorZ=300;restores=0;rays=0;auto& flags=p->data.boolFlags;flags.bits=10;downOriginalFlags=1;
  BeginDismount();assert(downActive&&Seat::offset==downOffset);
  assert(downOriginalFlags==2);flags.bits|=downFlags;
  auto expected=p->pos+Seat::offset;
  if(scenario==5){wallBlocked=true;expected=p->pos;} // Door closes mid-transition.
  if(scenario==6){p->pos.z+=20;floorZ+=10;expected.z=floorZ+2;}
  if(scenario==9){floorFound=false;expected=p->pos;}
  if(scenario==10){lowCeiling=true;expected=p->pos;}
  assert(Dismount());assert(!ridden&&!p->marker&&mount->rank==0&&targets.at(1).animation==0&&restores==2);
  assert(p->pos==expected&&flags.bits==10);
  if(scenario<3||scenario==5||scenario>=7)assert((p->pos==RE::NiPoint3{100,200,300}));
  else if(scenario==6)assert(p->pos.z==312);
  else assert(p->pos.x==(rightBlocked?-28:228)&&p->pos.z==302);
 }
 auto emergency=std::make_shared<RE::Actor>();ridden.value=emergency;targets[1]={};auto* ep=RE::PlayerCharacter::GetSingleton();ep->positionWrites=0;ep->marker=true;
 BeginDismount();ep->data.boolFlags.bits|=downFlags;
 assert(Dismount(true)&&!ridden&&!ep->marker&&emergency->positionWrites==0&&ep->positionWrites==0&&ep->data.boolFlags.bits==10);
 ridden.stale=true;auto* p=RE::PlayerCharacter::GetSingleton();p->marker=true;restores=0;
 upActive=true;downOriginalFlags=4;p->data.boolFlags.bits=15;
 assert(Dismount(true)&&!ridden&&!p->marker&&restores==1&&!upActive&&p->data.boolFlags.bits==12);
 std::puts("PASS: actual Dismount never refuses no floor, wall, missing controller; optional clear side and stale-handle cleanup. No airborne rejection in implementation.");
}
`;
fs.writeFileSync(path.join(repo,'build/dismount-regression.cpp'),code);
