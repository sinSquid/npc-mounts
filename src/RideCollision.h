#pragma once
#include <atomic>

namespace RideCollision {
// Physics callbacks must not inspect the mutable ride ledger or actor handles.
inline std::atomic<std::uint64_t> groups{0};
inline std::atomic<std::uint32_t> rejected{0};
inline bool Pair(std::uint64_t pair, std::uint32_t a, std::uint32_t b) {
    const auto player=static_cast<std::uint32_t>(pair>>32),mount=static_cast<std::uint32_t>(pair);
    return player&&mount&&player!=mount&&((a==player&&b==mount)||(a==mount&&b==player));
}
// Skyrim 1.5.97 RVA DD6780, caller B27570: RDX is an output byte pointer;
// R8/R9 are collidables and RAX returns the output pointer, NOT a bool.
using Filter=bool*(*)(const RE::hkpCollidableCollidableFilter*,bool*,const RE::hkpCollidable*,const RE::hkpCollidable*);
inline Filter original{};
inline bool* Check(const RE::hkpCollidableCollidableFilter* self,bool* result,const RE::hkpCollidable* a,const RE::hkpCollidable* b) {
    // Preserve the preceding hook's result and return pointer before narrowing.
    result=original(self,result,a,b);
    const auto pair=groups.load(std::memory_order_relaxed);
    if(*result&&pair&&Pair(pair,a->broadPhaseHandle.collisionFilterInfo.GetSystemGroup(),b->broadPhaseHandle.collisionFilterInfo.GetSystemGroup())){
        rejected.fetch_add(1,std::memory_order_relaxed);*result=false;
    }
    return result;
}
inline void Clear(){groups.store(0,std::memory_order_relaxed);}
inline void Update(RE::Actor* player,RE::Actor* mount){
    auto* p=player?player->GetCharController():nullptr;auto* m=mount?mount->GetCharController():nullptr;
    if(!p||!m){Clear();return;}
    RE::CFilter a{},b{};p->GetCollisionFilterInfo(a);m->GetCollisionFilterInfo(b);
    groups.store((std::uint64_t(a.GetSystemGroup())<<32)|b.GetSystemGroup(),std::memory_order_relaxed);
}
inline void Install(){
    REL::Relocation<std::uintptr_t> table{RE::VTABLE_bhkCollisionFilter[1]};
    original=reinterpret_cast<Filter>(table.write_vfunc(1,Check));
}
}
