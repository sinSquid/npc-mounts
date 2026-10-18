// Execute the real Dismount body with failed floor/wall queries, not a mock Dismount.
const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/plugin.cpp'),'utf8');
const start=source.indexOf('bool Dismount(');assert(start>=0);
let end=source.indexOf('{',start)+1,depth=1;
while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}
const code=`
#include <cassert>
#include "../src/Motion.h"
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
struct Actor {enum class BOOL_FLAGS:unsigned{};struct Flags{void reset(unsigned){}void set(BOOL_FLAGS){}};struct{Flags boolFlags;}data;
 auto& GetActorRuntimeData(){return data;}
 int id=1,rank=3;bool marker=true,hasController=true;unsigned resetEvents=0,positionWrites=0;NiPoint3 pos{100,200,300};Controller cc;
 auto GetPosition(){return pos;}void SetPosition(NiPoint3 p,bool){pos=p;++positionWrites;}float GetAngleZ(){return 0.f;}
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
float playerSpeedDelta=0;bool downActive=false,upActive=false;unsigned downFlags=0,downOriginalFlags=0;RE::NiPoint3 downOffset;HoldGate dismountHold;
namespace Seat{void Clear(){}}namespace MovementCompat{void End(){}}
namespace RideDrive{void End(){}}void ApplySpeed(RE::Actor*,float,float& delta){delta=0;}
int restores=0;void RestoreController(RE::Actor*,int,unsigned){++restores;}void StopMotion(RE::Actor*){}
bool floorFound=false,wallBlocked=false;int rays=0;
bool Ray(RE::bhkWorld*,RE::NiPoint3 from,RE::NiPoint3 to,RE::bhkPickData&){++rays;return from.z!=to.z?floorFound:wallBlocked;}
namespace spdlog{void info(const char*){}}
${source.slice(start,end)}
int main(){
 for(int scenario=0;scenario<4;scenario++){
  auto mount=std::make_shared<RE::Actor>();ridden.value=mount;targets[1]={};
  auto* p=RE::PlayerCharacter::GetSingleton();p->pos={100,200,300};p->marker=true;p->hasController=scenario!=2;
  floorFound=scenario!=0;wallBlocked=scenario==1;restores=0;rays=0;
  assert(Dismount());assert(!ridden&&!p->marker&&mount->rank==0&&targets.at(1).animation==0&&restores==2);
  if(scenario<3)assert((p->pos==RE::NiPoint3{100,200,300}));
  else assert(p->pos.x==196&&p->pos.z==302);
 }
 auto emergency=std::make_shared<RE::Actor>();ridden.value=emergency;targets[1]={};auto* ep=RE::PlayerCharacter::GetSingleton();ep->positionWrites=0;ep->marker=true;
 assert(Dismount(true)&&!ridden&&!ep->marker&&emergency->positionWrites==0&&ep->positionWrites==0);
 ridden.stale=true;auto* p=RE::PlayerCharacter::GetSingleton();p->marker=true;restores=0;
 assert(Dismount()&&!ridden&&!p->marker&&restores==1);
 std::puts("PASS: actual Dismount never refuses no floor, wall, missing controller; optional clear side and stale-handle cleanup. No airborne rejection in implementation.");
}
`;
fs.writeFileSync(path.join(repo,'build/dismount-regression.cpp'),code);
