#pragma once
#include "RideDrive.h"

namespace RideHooks {
// Only suppress the followed actor's movement inside its own update. No persistent
// pointer, no world-wide movement filter, and no effect on the physical player.
inline thread_local RE::Actor* placedActor=nullptr;
using Movement=void(*)(RE::Actor*,float);
using Camera=void(*)(RE::ThirdPersonState*,bool);
inline Movement originalMovement{};
inline Camera originalCamera{};
inline bool installed=false;
inline void MovementDelta(RE::Actor* actor,float delta){
    if(actor==placedActor&&placedActor)return;
    originalMovement(actor,delta);
}
inline void FreeRotation(RE::ThirdPersonState* state,bool sheathed){
    if(RideDrive::owned)state->freeRotationEnabled=true;
    else originalCamera(state,sheathed);
}
inline bool Install(){
    if(installed)return true;
    // SE 1.5.97: RCX=Actor*, XMM1=delta at RVA 5D8260 -> 5D87F0.
    // ThirdPersonState slot 0xD: RCX=this, DL=sheathed at RVA 84FA20.
    // Verified against installed executable and the upstream implementations.
    REL::Relocation<std::uintptr_t> call{REL::ID(36359),0xF0};
    // E8 rel32 stays a five-byte call after another plugin hooks this site.
    // Its displacement is NOT a version signature: write_call returns the
    // current destination, preserving the preceding plugin's trampoline.
    const auto* instruction=reinterpret_cast<const unsigned char*>(call.address());
    if(instruction[0]!=0xE8){
        spdlog::error("Ride movement requires E8 call at {:X}; found {:02X} {:02X} {:02X} {:02X} {:02X}; no patch applied",
            call.address(),instruction[0],instruction[1],instruction[2],instruction[3],instruction[4]);return false;
    }
    SKSE::AllocTrampoline(32);
    originalMovement=reinterpret_cast<Movement>(SKSE::GetTrampoline().write_call<5>(call.address(),MovementDelta));
    REL::Relocation<std::uintptr_t> camera{RE::VTABLE_ThirdPersonState[0]};
    originalCamera=reinterpret_cast<Camera>(camera.write_vfunc(0xD,FreeRotation));
    spdlog::info("Ride hooks installed: chained movement={:X}, camera={:X}",
        reinterpret_cast<std::uintptr_t>(originalMovement),reinterpret_cast<std::uintptr_t>(originalCamera));
    installed=true;return true;
}
}
