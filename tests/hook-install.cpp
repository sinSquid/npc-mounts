#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <cassert>
#include <cstdio>
#include <Windows.h>
namespace RideDrive { inline bool owned=false; }
namespace Fixture {
inline SKSE::Trampoline trampoline;
inline unsigned char* memory{};
inline std::uintptr_t table[16]{};
inline int allocations=0,writes=0;
struct Relocation {
 std::uintptr_t value;
 Relocation(REL::ID,std::ptrdiff_t offset):value(reinterpret_cast<std::uintptr_t>(memory+4)){assert(offset==0xf0);}
 template<class T> Relocation(T):value(reinterpret_cast<std::uintptr_t>(table)){}
 auto address()const{return value;}
 template<class F> std::uintptr_t write_vfunc(std::size_t slot,F f){++writes;auto old=table[slot];table[slot]=reinterpret_cast<std::uintptr_t>(f);return old;}
};
void AllocTrampoline(std::size_t size){assert(size==32);++allocations;trampoline.set_trampoline(memory+512,128);}
}
#include "../build/ride-hooks-fixture.h"
using Move=RideHooks::Movement;
int nativeCalls=0,firstCalls=0,secondCalls=0;float seenDelta=0;RE::Actor* seenActor{};
Move beforeOriginal{},afterOriginal{};
void Native(RE::Actor* actor,float delta){++nativeCalls;seenActor=actor;seenDelta=delta;}
void Before(RE::Actor* actor,float delta){++firstCalls;beforeOriginal(actor,delta);}
void After(RE::Actor* actor,float delta){++secondCalls;afterOriginal(actor,delta);}
void Camera(RE::ThirdPersonState*,bool){}
void Reset(){
 using namespace Fixture;std::memset(memory,0xcc,4096);
 // Real Win64 caller: preserve RCX/XMM1 and allocate shadow space.
 const unsigned char code[]{0x48,0x83,0xec,0x28,0xe8,0x8b,0x05,0,0,0x48,0x83,0xc4,0x28,0xc3};
 std::memcpy(memory,code,sizeof(code));
 // Original displacement reaches a native-function jump stub.
 unsigned char* stub=memory+9+0x58b;stub[0]=0xff;stub[1]=0x25;std::memset(stub+2,0,4);
 const auto target=reinterpret_cast<std::uintptr_t>(&Native);std::memcpy(stub+6,&target,8);
 table[0xd]=reinterpret_cast<std::uintptr_t>(&Camera);
 allocations=writes=nativeCalls=firstCalls=secondCalls=0;
 RideHooks::installed=false;RideHooks::placedActor=nullptr;
}
int main(){
 using namespace Fixture;
 memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(memory);
 auto invoke=reinterpret_cast<Move>(memory);auto* player=reinterpret_cast<RE::Actor*>(0x1000);auto* mount=reinterpret_cast<RE::Actor*>(0x2000);
 for(bool preceding:{false,true}){
  Reset();SKSE::Trampoline prior;prior.set_trampoline(memory+256,128);
  if(preceding)beforeOriginal=reinterpret_cast<Move>(prior.write_call<5>(reinterpret_cast<std::uintptr_t>(memory+4),Before));
  if(!RideHooks::Install()){
   assert(preceding&&allocations==0&&writes==0);
   std::puts("REPRODUCED: production installer rejects a valid preceding CommonLib E8 hook before any patch; expected old-version failure.");return 42;
  }
  assert(allocations==1&&writes==1);
  unsigned char installedCall[5];std::memcpy(installedCall,memory+4,5);
  assert(RideHooks::Install()&&allocations==1&&writes==1&&std::memcmp(installedCall,memory+4,5)==0);
  FlushInstructionCache(GetCurrentProcess(),memory,4096);
  invoke(player,.125f);assert(nativeCalls==1&&seenActor==player&&seenDelta==.125f&&firstCalls==int(preceding));
  RideHooks::placedActor=mount;invoke(mount,.25f);assert(nativeCalls==1&&firstCalls==int(preceding));
  invoke(player,.5f);assert(nativeCalls==2&&seenDelta==.5f);
  RideHooks::placedActor=nullptr;invoke(mount,.75f);assert(nativeCalls==3&&seenActor==mount);
  SKSE::Trampoline after;after.set_trampoline(memory+768,128);
  afterOriginal=reinterpret_cast<Move>(after.write_call<5>(reinterpret_cast<std::uintptr_t>(memory+4),After));
  FlushInstructionCache(GetCurrentProcess(),memory,4096);
  invoke(player,1.f);assert(secondCalls==1&&nativeCalls==4&&firstCalls==(preceding?4:0));
 }
 for(unsigned char opcode:{0x90,0xe9,0xff}){
  Reset();memory[4]=opcode;unsigned char saved[16];std::memcpy(saved,memory,16);
  assert(!RideHooks::Install());assert(!RideHooks::installed&&allocations==0&&writes==0&&std::memcmp(saved,memory,16)==0);
 }
 std::puts("PASS: production installer + real CommonLib trampolines + Win64 executable call: stock, preceding and following hooks, RCX/XMM1 forwarding, mount suppression, unrelated actor and release, unknown opcodes leave memory/vtable untouched. Not game QA.");
 VirtualFree(memory,0,MEM_RELEASE);
}
