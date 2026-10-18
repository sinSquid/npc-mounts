const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'src/plugin.cpp'),'utf8');
const start=source.indexOf('bool JumpInput('),end=source.indexOf('\nusing Talk =',start);
assert(start>0&&end>start);
assert(source.includes('jump.write_vfunc(1, JumpInput)'));
fs.writeFileSync(path.join(root,'build/ride-jump-regression.cpp'),`
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../src/Config.h"
namespace REL{struct Offset{unsigned value;};template<class T>struct Relocation{Relocation(Offset x){assert(x.value==0x1de2320);}float& operator*(){static float height=64;return height;}};}
namespace RE{
enum class CHARACTER_FLAGS{kCanJump};enum class hkpCharacterStateType{kOnGround,kJumping,kInAir};
struct JumpHandler{};struct ButtonEvent;
struct InputEvent{bool jump=true;bool QUserEvent(){return jump;}virtual ButtonEvent* AsButtonEvent(){return nullptr;}};
struct ButtonEvent:InputEvent{bool down=true;bool IsDown(){return down;}ButtonEvent* AsButtonEvent()override{return this;}};
struct UserEvents{bool jump=true;static UserEvents* GetSingleton(){static UserEvents e;return &e;}};
struct Controller{struct Flags{bool can=true;bool any(CHARACTER_FLAGS){return can;}}flags;float jumpHeight=0;hkpCharacterStateType wantState=hkpCharacterStateType::kOnGround;};
struct PlayerCharacter{Controller cc;bool loaded=true;float scale=1;auto* GetCharController(){return loaded?&cc:nullptr;}float GetScale(){return scale;}static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;}};
struct bhkWorld{static float GetWorldScale(){return .0142875f;}};
}
Settings::Data config;bool ridden=true,upActive=false,downActive=false,wasAir=false;float jumpTimer=0;struct{bool jumping=false;}rideMotion;
int humanCalls=0;bool originalJump(RE::JumpHandler*,RE::InputEvent*){++humanCalls;return true;}
unsigned jumpAudit=0,jumpAuditBudget=32;
${source.slice(start,end)}
int main(){auto* p=RE::PlayerCharacter::GetSingleton();RE::ButtonEvent press,release;release.down=false;
 for(float coefficient:{.8f,1.f,1.2f,2.f})for(float scale:{.5f,1.f,1.85f,3.f}){config.jumpHeightMultiplier=coefficient;p->scale=scale;jumpTimer=0;p->cc.wantState=RE::hkpCharacterStateType::kOnGround;
  assert(!JumpInput(nullptr,&press));assert(humanCalls==0&&jumpTimer==.2f&&p->cc.wantState==RE::hkpCharacterStateType::kJumping);
  assert(std::abs(p->cc.jumpHeight-64*scale*coefficient*.0142875f)<.00001f);
  auto height=p->cc.jumpHeight;assert(!JumpInput(nullptr,&press)&&p->cc.jumpHeight==height);}
 jumpTimer=0;assert(!JumpInput(nullptr,&release)&&jumpTimer==0);assert(!JumpInput(nullptr,nullptr));
 config.allowJump=false;assert(!JumpInput(nullptr,&press)&&jumpTimer==0);config.allowJump=true;
 wasAir=true;assert(!JumpInput(nullptr,&press)&&jumpTimer==0);wasAir=false;
 rideMotion.jumping=true;assert(!JumpInput(nullptr,&press)&&jumpTimer==0);rideMotion.jumping=false;
 p->cc.flags.can=false;assert(!JumpInput(nullptr,&press)&&jumpTimer==0);p->cc.flags.can=true;
 p->loaded=false;assert(!JumpInput(nullptr,&press)&&jumpTimer==0);p->loaded=true;
 ridden=false;assert(JumpInput(nullptr,&press)&&humanCalls==1);
 puts("PASS: actual JumpInput blocks native human handler while riding, controller-only jump, scale, repeats, airborne, config and unmounted delegation. Not game QA.");}
`);
