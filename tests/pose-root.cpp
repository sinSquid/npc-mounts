#define NOMINMAX
#include <Windows.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace RE {
struct Actor;struct NiPoint3{float x=0,y=0,z=0;};using FormID=unsigned;
struct hkbGenerator{};struct hkbBehaviorGraph{int id;};struct hkbContext{int marker;};
struct hkbGeneratorOutput{int pose;int physics;};
}
namespace spdlog {template<class... T>void info(const char*,T...){}template<class... T>void error(const char*,T...){} }
std::uintptr_t executable=0;
namespace REL {struct Offset{std::uintptr_t value;};template<class T>struct Relocation{explicit Relocation(Offset p){assert(p.value==0x9F1230);}std::uintptr_t address(){return executable;}};}
#include "../src/PoseOutput.h"
int nativeCalls=0,crawlCalls=0,seatCalls=0;bool expectedSet=false,expectedUpdate=false;float expectedOffset=0;
void Native(RE::hkbBehaviorGraph* graph,const RE::hkbContext& context,RE::hkbGeneratorOutput& output,bool setPose,float offset,bool update){
    assert(graph->id==7&&context.marker==123&&setPose==expectedSet&&offset==expectedOffset&&update==expectedUpdate);
    ++nativeCalls;output={10,99};
}
namespace Crawl {void GeneratePose(RE::hkbGenerator*,const RE::hkbContext&,const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput& output,float offset){assert(offset==expectedOffset&&output.pose==10);++crawlCalls;output.pose=20;}}
namespace Seat {void GeneratePose(RE::hkbGenerator*,const RE::hkbContext&,const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput& output,float offset){assert(offset==expectedOffset&&output.pose==20);++seatCalls;output.pose=30;}}
int main(){
    auto* page=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(page);executable=reinterpret_cast<std::uintptr_t>(page);
    // Actual engine entry prologue, followed by an independent native surrogate.
    const unsigned char entry[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0xff,0x25,0,0,0,0};
    std::memcpy(page,entry,sizeof(entry));auto destination=&Native;std::memcpy(page+sizeof(entry),&destination,sizeof(destination));
    page[0]=0x90;assert(!PoseOutput::Install());page[0]=entry[0];
    assert(PoseOutput::Install());auto invoke=reinterpret_cast<PoseOutput::Generate>(page);
    RE::hkbBehaviorGraph graph{7};RE::hkbContext context{123};RE::hkbGeneratorOutput output{};
    for(bool setPose:{false,true})for(bool update:{false,true})for(float offset:{0.f,.125f}){
        expectedSet=setPose;expectedUpdate=update;expectedOffset=offset;
        const auto calls=nativeCalls,modified=seatCalls;invoke(&graph,context,output,setPose,offset,update);
        assert(nativeCalls==calls+1&&seatCalls==modified+(setPose?0:1)&&crawlCalls==seatCalls);
        assert(output.pose==(setPose?10:30)&&output.physics==99);
    }
    assert(MH_DisableHook(page)==MH_OK&&MH_RemoveHook(page)==MH_OK);MH_Uninitialize();VirtualFree(page,0,MEM_RELEASE);
    std::puts("PASS: production root Install/GenerateRoot with real MinHook and engine entry bytes; six-argument forwarding, final order, no virtual subgraph needed, physics preserved. Not game QA.");
}
