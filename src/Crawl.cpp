#include "pch.h"
#include "Crawl.h"
#include <array>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace Crawl {
constexpr unsigned bones=99,clipCount=7;
struct Pose{float t[4],q[4],s[4];};
using Frame=std::array<Pose,bones>;
struct Clip{float duration{};std::vector<Frame> frames;};
std::array<Clip,clipCount> clips;
std::array<std::array<char,64>,bones> names;
std::array<std::int16_t,bones> parents;
struct Graph{RE::BSTSmartPointer<RE::BShkbAnimationGraph> graph;unsigned count{};std::array<unsigned,bones> map;};
struct ActorPose{const RE::NiAVObject* root{};std::vector<Graph> graphs;Frame pose;int gait=0;float time=0;bool wrote=false;};
std::unordered_map<RE::FormID,ActorPose> actors;
struct Retry{const RE::NiAVObject* root{};float remaining=0;};
std::unordered_map<RE::FormID,Retry> retries;
std::mutex mutex;
bool loaded=false;
using Generate=void(*)(RE::hkbGenerator*,const RE::hkbContext&,const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float);
Generate original{};

void Blend(const Frame& a,const Frame& b,float t,Frame& out){
    for(unsigned i=0;i<bones;++i){
        float dot=0,norm=0;for(unsigned j=0;j<4;++j)dot+=a[i].q[j]*b[i].q[j];
        for(unsigned j=0;j<4;++j){
            out[i].t[j]=a[i].t[j]+t*(b[i].t[j]-a[i].t[j]);
            out[i].s[j]=a[i].s[j]+t*(b[i].s[j]-a[i].s[j]);
            out[i].q[j]=a[i].q[j]+t*((dot<0?-b[i].q[j]:b[i].q[j])-a[i].q[j]);norm+=out[i].q[j]*out[i].q[j];
        }
        for(float& v:out[i].q)v/=std::sqrt(norm);
    }
}
void Sample(const Clip& clip,float time,Frame& out){
    const float f=std::fmod(std::max(time,0.f),clip.duration)/clip.duration*(clip.frames.size()-1);
    const auto a=static_cast<unsigned>(f),b=std::min(a+1,static_cast<unsigned>(clip.frames.size()-1));
    Blend(clip.frames[a],clip.frames[b],f-a,out);
}
std::string_view Canonical(std::string_view name){
    // Vanilla and XPMSSE differ on this helper, not on deforming bones.
    return name=="x_NPC LookNode [Look]"?"NPC LookNode [Look]":name;
}
void GeneratePose(RE::hkbGenerator* self,const RE::hkbContext& context,const RE::hkbGeneratorOutput** children,RE::hkbGeneratorOutput& output,float offset){
    original(self,context,children,output,offset);
    std::scoped_lock lock(mutex);
    for(auto& [id,actor]:actors)for(const auto& binding:actor.graphs){
        if(&binding.graph->characterInstance!=context.character)continue;
        if(!output.tracks)return;
        const auto& header=output.tracks->masterHeader;
        if(header.numTracks<3||header.numTracks>128||header.numBytes<16+header.numTracks*16)return;
        const auto& track=output.tracks->trackHeaders[2];
        if(track.numData<bones||track.numData>binding.count||track.capacity<track.numData||track.elementSizeBytes!=sizeof(Pose)||
            static_cast<int>(track.flags)!=0||static_cast<int>(track.type)!=1||track.onFraction<=0||
            track.dataOffset<16+header.numTracks*16||track.dataOffset>header.numBytes||
            track.numData*sizeof(Pose)>header.numBytes-track.dataOffset)return;
        auto* data=reinterpret_cast<std::byte*>(output.tracks)+track.dataOffset;
        if(std::any_of(binding.map.begin(),binding.map.end(),[&](unsigned index){return index>=static_cast<unsigned>(track.numData);}))return;
        for(unsigned i=0;i<bones;++i)std::memcpy(data+binding.map[i]*sizeof(Pose),&actor.pose[i],sizeof(Pose));
        if(!actor.wrote){actor.wrote=true;spdlog::info("Native crawl output active ref={:08X} tracks={}",id,track.numData);}
        // Only animation output is touched, before skin/SMP publication.
        // Unmapped custom bones and the character controller remain untouched.
        return;
    }
}
bool Install(){
    std::ifstream file("Data/SKSE/Plugins/NPCMounts/Crawl.nmc",std::ios::binary);
    std::array<std::uint32_t,4> header{};file.read(reinterpret_cast<char*>(header.data()),sizeof(header));
    if(header!=std::array<std::uint32_t,4>{0x434d504e,1,bones,clipCount})return false;
    file.read(reinterpret_cast<char*>(names.data()),sizeof(names));file.read(reinterpret_cast<char*>(parents.data()),sizeof(parents));
    for(unsigned i=0;i<bones;++i)if(names[i][63]!=0||parents[i]<-1||parents[i]>=static_cast<int>(i))return false;
    for(auto& clip:clips){
        std::uint32_t count=0;file.read(reinterpret_cast<char*>(&clip.duration),4);file.read(reinterpret_cast<char*>(&count),4);
        if(!file||!std::isfinite(clip.duration)||clip.duration<=0||count<2||count>2048)return false;
        clip.frames.resize(count);file.read(reinterpret_cast<char*>(clip.frames.data()),count*sizeof(Frame));if(!file)return false;
        for(const auto& frame:clip.frames)for(const auto& p:frame){
            float norm=0;for(float v:p.q){if(!std::isfinite(v))return false;norm+=v*v;}if(std::abs(norm-1)>0.01f)return false;
            for(float v:p.t)if(!std::isfinite(v))return false;
            for(unsigned j=0;j<3;++j)if(!std::isfinite(p.s[j])||p.s[j]<=0)return false;
        }
    }
    if(file.peek()!=EOF)return false;
    REL::Relocation<std::uintptr_t> graph{RE::VTABLE_hkbBehaviorGraph[0]};
    original=reinterpret_cast<Generate>(graph.write_vfunc(0x17,GeneratePose));loaded=true;
    return true;
}
bool Bind(RE::Actor* actor){
    if(!actor||!loaded)return false;
    ActorPose result;result.root=actor->Get3D();result.pose=clips[0].frames.front();
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if(!result.root||!actor->GetAnimationGraphManager(manager)||!manager)return false;
    for(auto& graph:manager->graphs){
        if(!graph||!graph->characterInstance.setup)continue;
        auto skeleton=graph->characterInstance.setup->animationSkeleton;
        if(!skeleton||skeleton->bones.size()<bones||skeleton->parentIndices.size()!=skeleton->bones.size())continue;
        Graph binding{graph,static_cast<unsigned>(skeleton->bones.size()),{}};bool valid=true;
        for(unsigned i=0;i<bones&&valid;++i){
            bool found=false;
            for(unsigned j=0;j<binding.count;++j)if(skeleton->bones[j].name.c_str()&&Canonical(skeleton->bones[j].name.c_str())==Canonical(names[i].data())){binding.map[i]=j;found=true;break;}
            if(!found){valid=false;break;}
            const int expected=parents[i]<0?-1:static_cast<int>(binding.map[parents[i]]);
            if(skeleton->parentIndices[binding.map[i]]!=expected)valid=false;
        }
        if(valid)result.graphs.push_back(std::move(binding));
    }
    if(result.graphs.empty()){
        std::scoped_lock lock(mutex);auto& retry=retries[actor->GetFormID()];
        if(retry.root!=result.root)spdlog::warn("Crawl skeleton incompatible ref={:08X}",actor->GetFormID());
        retry={result.root,1.f};actors.erase(actor->GetFormID());return false;
    }
    std::scoped_lock lock(mutex);retries.erase(actor->GetFormID());actors.insert_or_assign(actor->GetFormID(),std::move(result));return true;
}
void Update(RE::Actor* actor,int gait,float delta){
    if(!actor)return;
    {std::scoped_lock lock(mutex);auto it=retries.find(actor->GetFormID());if(it!=retries.end()&&it->second.root==actor->Get3D()){
        it->second.remaining-=std::clamp(delta,0.f,.1f);if(it->second.remaining>0)return;
    }}
    bool bind=false;{std::scoped_lock lock(mutex);auto it=actors.find(actor->GetFormID());bind=it==actors.end()||it->second.root!=actor->Get3D();}
    if(bind&&!Bind(actor))return;
    std::scoped_lock lock(mutex);auto& state=actors.at(actor->GetFormID());
    gait=std::clamp(gait,0,6);if(state.gait!=gait){
        // Walk/run/sprint share a cycle. Carry phase instead of restarting frame zero.
        const bool forwardCycle=state.gait>=1&&state.gait<=3&&gait>=1&&gait<=3;
        state.time=forwardCycle?state.time/clips[state.gait].duration*clips[gait].duration:0.f;
        state.gait=gait;
    }
    state.time=std::fmod(state.time+std::clamp(delta,0.f,.1f),clips[gait].duration);
    Frame next;Sample(clips[gait],state.time,next);
    // Frame-rate independent short blend; no scene-node interpolation or teleport.
    Blend(state.pose,next,1.f-std::exp(-20.f*std::clamp(delta,0.f,.1f)),state.pose);
}
void Remove(RE::FormID id){std::scoped_lock lock(mutex);actors.erase(id);retries.erase(id);}
void Clear(){std::scoped_lock lock(mutex);actors.clear();retries.clear();}
}
