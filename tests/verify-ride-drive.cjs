const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'src/plugin.cpp'),'utf8');
assert(source.includes('RideDrive::Before(player,delta,upActive||downActive)'));
assert(source.indexOf('RideDrive::Before(player,delta')<source.indexOf('originalUpdate(player, delta)'));
const npc=source.slice(source.indexOf('void NPCUpdate('),source.indexOf('using POV ='));
assert.equal((npc.match(/SyncMount\(/g)||[]).length,0);
const playerUpdate=source.slice(source.indexOf('void PlayerUpdate('),source.indexOf('void PlayerUpdateInMain('));
assert.equal((playerUpdate.match(/SyncMount\(/g)||[]).length,1);
assert(playerUpdate.indexOf('originalPlayerUpdate(')<playerUpdate.indexOf('SyncMount('));
assert(!source.slice(source.indexOf('void Tick('),source.indexOf('using Update =')).includes('SyncMount('));
assert(source.includes('rideMotion.jumping||wasAir||jumpTimer>0.f'));
fs.writeFileSync(path.join(root,'build/ride-drive-regression.cpp'),`
#include <cassert>
#include <cstdio>
namespace RE {
struct Actor{float angle=0;float GetAngleZ(){return angle;}void SetHeading(float x){angle=x;}};
struct PlayerControls{struct Point{float x=0,y=0;};struct Data{Point moveInputVec,prevMoveVec,lookInputVec;bool fovSlideMode=false;}data;static PlayerControls* GetSingleton(){static PlayerControls p;return &p;}};
}
#include "../src/RideDrive.h"
int main(){auto& d=RE::PlayerControls::GetSingleton()->data;
 for(float fps:{30.f,60.f,144.f})for(bool old:{false,true}){
  d.fovSlideMode=old;RideDrive::Begin();assert(d.fovSlideMode==old);RideDrive::Begin();
  RE::Actor p;RideDrive::right=true;d.moveInputVec={1,-1};d.prevMoveVec={1,1};d.lookInputVec={.4f,.2f};
  for(int i=0;i<fps;++i)RideDrive::Before(&p,1/fps,false);
  assert(std::abs(p.angle-1.5f)<.0001f);assert(d.moveInputVec.x==0&&d.prevMoveVec.x==0&&d.moveInputVec.y==-1);
  assert(d.lookInputVec.x==.4f&&d.lookInputVec.y==.2f&&d.fovSlideMode==old); // No stationary POV-slide writes.
  RideDrive::left=true;auto yaw=p.angle;RideDrive::Before(&p,.1f,false);assert(yaw==p.angle);
  RideDrive::left=false;RideDrive::Before(&p,.1f,true);assert(yaw==p.angle);
  RideDrive::ResetInput();assert(!RideDrive::left&&!RideDrive::right&&RideDrive::stick==0);
  RideDrive::stick=-1;RideDrive::Before(&p,.1f,false);assert(std::abs(p.angle-yaw+.15f)<.0001f);
  RideDrive::End();assert(d.fovSlideMode==old&&!RideDrive::owned);RideDrive::End();assert(d.fovSlideMode==old);
 }
 for(float fps:{30.f,60.f,144.f}){
  RideDrive::Begin();RE::Actor p;float yaw=1.f;
  for(int i=0;i<fps;++i){RideDrive::Before(&p,1/fps,false);RideDrive::Aim(&p,yaw,1/fps);assert(std::abs(p.angle+yaw-1.f)<.0001f);}
  assert(std::abs(p.angle-1.f)<.0001f&&std::abs(yaw)<.0001f);
  RideDrive::left=true;const float heading=p.angle;RideDrive::Aim(&p,yaw,.1f);assert(p.angle==heading);
  RideDrive::End();yaw=2;RideDrive::Aim(&p,yaw,.1f);assert(p.angle==heading&&yaw==2);
 }
 puts("PASS: steering time-based; camera world yaw preserved, manual override, unmounted no-op; no strafe or POV-slide writes. Not engine QA.");}
`);
