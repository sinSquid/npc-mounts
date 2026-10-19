#pragma once
#include "Seat.h"
#include "Crawl.h"
#include <MinHook.h>
#include <array>
#include <cstring>

namespace PoseOutput {
// SE 1.5.97 non-virtual root generator. RCX=graph, RDX=context, R8=output,
// R9B=setCharacterPose; float timeOffset and bool update on the stack.
// The actor publication path passes setCharacterPose=false and consumes output
// afterwards. The virtual 0x17 only copies a referenced child graph's output.
using Generate=void(*)(RE::hkbBehaviorGraph*,const RE::hkbContext&,RE::hkbGeneratorOutput&,bool,float,bool);
inline Generate original{};
inline void GenerateRoot(RE::hkbBehaviorGraph* graph,const RE::hkbContext& context,RE::hkbGeneratorOutput& output,bool setPose,float offset,bool update){
    original(graph,context,output,setPose,offset,update);
    if(setPose)return; // Internal evaluations that already copied a character pose are not actor publication.
    Crawl::GeneratePose(nullptr,context,nullptr,output,offset);
    Seat::GeneratePose(nullptr,context,nullptr,output,offset);
}
inline bool Install(){
    const auto target=REL::Relocation<std::uintptr_t>{REL::Offset(0x9F1230)}.address();
    constexpr std::array<unsigned char,15> entry{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18};
    if(std::memcmp(reinterpret_cast<const void*>(target),entry.data(),entry.size())!=0){spdlog::error("Pose root entry differs at {:X}; no pose hook installed",target);return false;}
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    auto* address=reinterpret_cast<void*>(target);
    const auto create=MH_CreateHook(address,reinterpret_cast<void*>(&GenerateRoot),reinterpret_cast<void**>(&original));
    if(create!=MH_OK){spdlog::error("Pose root detour creation failed {}",int(create));return false;}
    const auto enable=MH_EnableHook(address);
    if(enable!=MH_OK){MH_RemoveHook(address);original=nullptr;spdlog::error("Pose root detour enabling failed {}",int(enable));return false;}
    spdlog::info("Pose output attached to non-virtual root generator at {:X}",target);return true;
}
}
