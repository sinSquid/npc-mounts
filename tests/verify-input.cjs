const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/plugin.cpp'),'utf8');
function extract(signature){const start=source.indexOf(signature);assert(start>=0);let end=source.indexOf('{',start)+1,depth=1;while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}return source.slice(start,end);}
const code=`
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_set>
#include "../src/Config.h"
#include "../src/RideControl.h"
Settings::Data config;HoldGate dismountHold;bool downActive=false,upActive=false;
namespace RE {
enum class INPUT_DEVICE {kKeyboard,kMouse,kGamepad};enum class BSEventNotifyControl{kContinue};
template<class T>struct BSTEventSource{};
template<class T>struct BSTEventSink{virtual BSEventNotifyControl ProcessEvent(T const*,BSTEventSource<T>*)=0;};
struct ButtonEvent;struct ThumbstickEvent;
struct InputEvent {InputEvent* next{};virtual ButtonEvent* AsButtonEvent(){return nullptr;}virtual ThumbstickEvent* AsThumbstickEvent(){return nullptr;}};
struct ButtonEvent:InputEvent {std::string action;unsigned key;bool up=false,held=false;INPUT_DEVICE device=INPUT_DEVICE::kKeyboard;
 ButtonEvent(std::string a,unsigned k,bool u=false):action(a),key(k),up(u){}
 ButtonEvent* AsButtonEvent()override{return this;}bool IsUp(){return up;}bool IsDown(){return !up&&!held;}
 const auto& QUserEvent(){return action;}auto GetDevice(){return device;}auto GetIDCode(){return key;}
};
struct ThumbstickEvent:InputEvent {float xValue=1,yValue=-1;bool left=true;ThumbstickEvent* AsThumbstickEvent()override{return this;}bool IsLeft(){return left;}};
struct UserEvents {
 enum class INPUT_CONTEXT_ID{kGameplay,kMenu};
 std::string sneak="Sneak",back="Back",shout="Shout",readyWeapon="Ready Weapon",togglePOV="Toggle POV",leftAttack="Left",rightAttack="Right",dualAttack="Dual",activate="Activate",jump="Jump",forward="Forward",strafeLeft="Strafe Left",strafeRight="Strafe Right";
 static UserEvents* GetSingleton(){static UserEvents e;return &e;}
};
struct PlayerCharacter {void *left{},*right{};void* GetEquippedObject(bool l)const{return l?left:right;}static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;}};
struct UI {bool paused=false;bool GameIsPaused(){return paused;}static UI* GetSingleton(){static UI u;return &u;}};
struct ControlMap {std::vector<UserEvents::INPUT_CONTEXT_ID> contextPriorityStack{UserEvents::INPUT_CONTEXT_ID::kGameplay};auto& GetRuntimeData(){return *this;}static ControlMap* GetSingleton(){static ControlMap m;return &m;}};
}
bool ridden=true;int spell;void* restoreSpell=&spell;std::unordered_set<std::uint64_t> suppressedButtons;
${extract('bool BlockRideButton(')}
${extract('struct RideInput final')} rideInput;
void send(RE::InputEvent*& head){rideInput.ProcessEvent(&head,nullptr);}
int main(){
 for(auto key:{0x1Du,0x9Du,0x38u,0xB8u}){
  RE::ButtonEvent down("",key),up("",key,true);RE::InputEvent* head=&down;send(head);assert(!head);
  head=&up;send(head);assert(!head&&suppressedButtons.empty());
 }
 RE::ButtonEvent jump("Jump",57),sneak("Sneak",46),back("Back",31),exit("Activate",18);
 jump.next=&sneak;sneak.next=&back;back.next=&exit;RE::InputEvent* head=&jump;send(head);
 assert(head==&jump&&!head->next&&dismountHold.pressed);
 assert(!dismountHold.Tick(.25f,.25f)&&dismountHold.Tick(.001f,.25f));
 RE::ButtonEvent exitUp("Activate",18,true);head=&exitUp;send(head);assert(!head&&!dismountHold.pressed);
 RE::ButtonEvent preHeldRelease("Sneak",29,true);head=&preHeldRelease;send(head);assert(head==&preHeldRelease);
 RE::ThumbstickEvent stick;head=&stick;send(head);assert(stick.yValue==0);
 config.allowBackward=true;stick.yValue=-1;head=&stick;send(head);assert(stick.yValue==-1);config.allowBackward=false;
 config.allowBackward=true;RE::ButtonEvent altBack("Back",0x38);head=&altBack;send(head);assert(head==&altBack);config.allowBackward=false;
 config.blockSneak=false;RE::ButtonEvent allowedCtrl("Sneak",29);head=&allowedCtrl;send(head);assert(head==&allowedCtrl);config.blockSneak=true;
 downActive=true;stick.xValue=1;stick.yValue=1;head=&stick;send(head);assert(stick.xValue==0&&stick.yValue==0);downActive=false;
 RE::ButtonEvent attack("Left",1);attack.device=RE::INPUT_DEVICE::kMouse;
 RE::PlayerCharacter::GetSingleton()->left=restoreSpell;head=&attack;send(head);assert(head==&attack);
 RE::PlayerCharacter::GetSingleton()->left=nullptr;head=&attack;send(head);assert(!head);
 RE::ControlMap::GetSingleton()->contextPriorityStack.back()=RE::UserEvents::INPUT_CONTEXT_ID::kMenu;
 RE::ButtonEvent menuCtrl("",29);head=&menuCtrl;send(head);assert(head==&menuCtrl);
 RE::ControlMap::GetSingleton()->contextPriorityStack.back()=RE::UserEvents::INPUT_CONTEXT_ID::kGameplay;
 ridden=false;RE::ButtonEvent normal("Sneak",29);head=&normal;send(head);assert(head==&normal);
 std::puts("PASS: actual input filter Ctrl/Alt press+release, mixed chain preserves jump/exit, backward stick, B casting, menus and unmounted input.");
}
`;
fs.writeFileSync(path.join(repo,'build/input-regression.cpp'),code);
