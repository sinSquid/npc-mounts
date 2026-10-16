const fs=require('fs'),path=require('path'),assert=require('assert');const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'src/plugin.cpp'),'utf8');
assert(source.includes('RideCollision::Update(player,actor);'));assert(source.includes('RideCollision::Clear();Mounts::Revert(s);'));
assert(source.slice(source.indexOf('bool Dismount('),source.indexOf('void BeginDismount(',source.indexOf('bool Dismount('))).includes('RideCollision::Clear();'));
fs.writeFileSync(path.join(root,'build/ride-collision-regression.cpp'),`
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <type_traits>
namespace RE{
struct CFilter{unsigned group=0;unsigned GetSystemGroup()const{return group;}};
struct Controller{CFilter filter;void GetCollisionFilterInfo(CFilter& f){f=filter;}};
struct Actor{Controller* cc=nullptr;Controller* GetCharController(){return cc;}};
struct hkpCollidableCollidableFilter{};struct hkpCollidable{struct{CFilter collisionFilterInfo;}broadPhaseHandle;};
inline std::uintptr_t VTABLE_bhkCollisionFilter[2]{};
}
namespace REL{template<class T>struct Relocation{Relocation(std::uintptr_t){}template<class F>std::uintptr_t write_vfunc(int,F){return 0;}};}
#include "../src/RideCollision.h"
using NativeContract=bool*(*)(const RE::hkpCollidableCollidableFilter*,bool*,const RE::hkpCollidable*,const RE::hkpCollidable*);
static_assert(std::is_same_v<decltype(&RideCollision::Check),NativeContract>);
int calls=0;bool allow=true,alternate=false,otherResult=true;
bool* Original(const RE::hkpCollidableCollidableFilter*,bool* result,const RE::hkpCollidable*,const RE::hkpCollidable*){++calls;if(alternate)result=&otherResult;*result=allow;return result;}
#ifdef ABI_BRIDGE
extern "C" {
void* observedSelf=nullptr;void* observedResult=nullptr;void* observedA=nullptr;void* observedB=nullptr;
bool* InvokeNative(void* hook,void* self,bool* result,void* a,void* b);
bool* NativeOriginal(const RE::hkpCollidableCollidableFilter*,bool*,const RE::hkpCollidable*,const RE::hkpCollidable*);
}
#endif
int main(){using namespace RideCollision;original=Original;RE::Controller pc{{12}},mc{{31}};RE::Actor p{&pc},m{&mc};RE::hkpCollidable a{{{12}}},b{{{31}}},wall{{{0}}},other{{{44}}};
 auto check=[&](RE::hkpCollidable& x,RE::hkpCollidable& y){struct{unsigned char pre=0xA5;bool result=false;unsigned char post=0x5A;}out;auto* returned=Check(nullptr,&out.result,&x,&y);assert(returned==(alternate?&otherResult:&out.result));assert(out.pre==0xA5&&out.post==0x5A);return *returned;};
 Update(&p,&m);assert(!check(a,b));assert(!check(b,a));assert(calls==2);assert(check(a,wall));assert(check(b,other));assert(check(a,a));
 Clear();assert(check(a,b));Update(&p,nullptr);assert(groups==0);Update(&p,&m);mc.filter.group=52;Update(&p,&m);assert(check(a,b));b.broadPhaseHandle.collisionFilterInfo.group=52;assert(!check(a,b));
 mc.filter.group=12;Update(&p,&m);assert(check(a,a));pc.filter.group=0;Update(&p,&m);assert(check(a,b));
 allow=false;assert(!check(a,wall));allow=true;alternate=true;assert(check(a,wall));alternate=false;
 for(int i=0;i<10000;i++){pc.filter.group=12;mc.filter.group=52;Update(&p,&m);assert(!check(a,b));Clear();assert(check(a,b));}
#ifdef ABI_BRIDGE
 original=NativeOriginal;RE::hkpCollidableCollidableFilter self;
 struct{unsigned char pre=0xA5;bool result=false;unsigned char post=0x5A;}out;
 for(int i=0;i<10000;i++){
  if(i%2)Update(&p,&m);else Clear();
  auto* returned=InvokeNative(reinterpret_cast<void*>(&Check),&self,&out.result,&a,&b);
  assert(returned==&out.result&&out.pre==0xA5&&out.post==0x5A&&out.result==!(i%2));
  assert(observedSelf==&self&&observedResult==&out.result&&observedA==&a&&observedB==&b);
 }
 puts("PASS: independent MASM caller/callee validates RCX=this RDX=result R8=A R9=B, pointer return, byte guards, native-original forwarding, 10000 hooked/unhooked-pair calls.");
#endif
 puts("PASS: actual ride filter rejects only symmetric nonzero distinct ride groups; preserves scenery/others/original callback; clear, missing controller, rebuilt groups, 10000 lifecycle cycles.");}
`);
