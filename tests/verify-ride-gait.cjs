const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..');
const src=fs.readFileSync(process.argv[2]||path.join(root,'src/plugin.cpp'),'utf8');
assert(src.includes('rideMotion.Update(speed,movement.x,movement.y'));
assert(!src.includes('if (jumpTimer > 0.f || air) animation = 2'));
assert.equal((src.match(/rideMotion\s*=\s*\{\}/g)||[]).length,4); // Mount, both Dismount exits, Revert
const speedStart=src.indexOf('float speed = std::hypot',src.indexOf('void Tick('));
const speedEnd=src.indexOf('    const bool air =',speedStart);
assert(speedStart>=0&&speedEnd>speedStart);
fs.writeFileSync(path.join(root,'build/ride-gait-regression.cpp'),`
#include <cassert>
#include <cstdio>
#include "../src/Motion.h"
namespace RE{
enum class hkpCharacterStateType{kOnGround=0,kInAir=2,kSwimming=5};
struct bhkWorld{static float GetWorldScaleInverse(){return 70.f;}};
}
struct Vector{struct{float m128_f32[4]{};}quad;};
struct Controller{struct{RE::hkpCharacterStateType currentState;}context;Vector outVelocity;};
float ActualSpeed(Controller* cc,Vector velocity){
${src.slice(speedStart,speedEnd)}
return speed;
}
int main(){for(float fps:{30.f,60.f,144.f}){
 const float dt=1/fps;Motion::RideState s;
 auto step=[&](float speed,float x,float y,bool air=false,bool jump=false,float yaw=0.f){return s.Update(speed,x,y,yaw,air,jump,dt,180,450);};
 // Recorded idle noise (43.1/54.32), including ground/air flicker and extreme residual speed.
 for(int i=0;i<1000;++i)assert(step(i%2?43.1f:751.83f,0,0,i%2)==0);
 assert(step(120,0,1)==1);
 for(int i=0;i<fps;++i)step(300,0,1);assert(s.animation==2);
 // Alternating full-speed/low-speed corrections must not restart walk/run every frame.
 for(int i=0;i<1000;++i)assert(step(i%2?120:300,0,1)==2);
 for(int i=0;i<fps;++i)step(600,0,1);assert(s.animation==3);
 assert(step(43.1f,0,0)==0);
 assert(step(100,1,0)==1); // Strafe is real input, not only forward.
 for(int i=0;i<fps;++i)step(100,0,-1);assert(s.animation==4);
 assert(step(500,.01f,.01f)==0); // Stick dead zone.
 assert(step(0,0,0,true)==0); // Falling/contact noise is not a requested jump.
 assert(step(0,0,0,true,true)==2); // Confirmed stationary jump.
 for(int i=0;i<fps*3;++i)assert(step(0,0,0,true)==2); // Airborne beyond original .2s request.
 assert(step(0,0,0,false)==2);assert(step(0,0,0,true)==2); // One-frame contact is not landing.
 for(int i=0;i<fps;++i)step(43.1f,0,0,false);assert(s.animation==0&&!s.jumping);
 assert(step(120,0,1)==1);assert(step(120,0,1,true,true)==2);
 for(int i=0;i<fps;++i)step(120,0,1,false);assert(s.animation==1&&!s.jumping);
 s={};assert(step(0,0,0)==0);
 assert(step(0,0,0,false,false,.7f)==6); // Intentional stationary turning retained.
 // Replay logged 01:50:52 water state: linear=(0,0,0), nonzero horizontal output.
 Controller cc{{RE::hkpCharacterStateType::kSwimming},{{{-9.4795f,-7.5169f,-5.9101f,0}}}};
 Vector linear{};const float waterSpeed=ActualSpeed(&cc,linear);assert(waterSpeed>540.f);
 Motion::RideState water;
 for(int i=0;i<fps;++i)water.Update(waterSpeed,0,1,0,false,false,dt,230,540);
 assert(water.animation==3);
 assert(water.Update(waterSpeed,0,0,0,false,false,dt,230,540)==0); // Stop intent still wins.
 for(int i=0;i<fps;++i)water.Update(waterSpeed,0,-1,0,false,false,dt,230,540);
 assert(water.animation==4);
 cc.outVelocity.quad.m128_f32[0]=cc.outVelocity.quad.m128_f32[1]=0;
 assert(ActualSpeed(&cc,linear)==0); // Vertical water correction alone is not locomotion.
 cc.outVelocity.quad.m128_f32[0]=12;linear.quad.m128_f32[0]=3;
 assert(ActualSpeed(&cc,linear)==210); // Preserve valid linear velocity.
 linear={};cc.context.currentState=RE::hkpCharacterStateType::kOnGround;assert(ActualSpeed(&cc,linear)==0);
 cc.context.currentState=RE::hkpCharacterStateType::kInAir;assert(ActualSpeed(&cc,linear)==0);
 }
 puts("PASS: production RideState, recorded idle noise, strafe/backward/deadzone, gait debounce, confirmed jump/landing/reset at 30/60/144 FPS; not engine QA.");}
`);
