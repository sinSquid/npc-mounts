#include "pch.h"
#include "Crawl.h"
#include "PosePoint.h"
#include <array>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace Crawl {
constexpr unsigned bones=99,clipCount=8,boundClip=7;
struct Pose{float t[4],q[4],s[4];};
using Frame=std::array<Pose,bones>;
struct Clip{float duration{};std::vector<Frame> frames;};
std::array<Clip,clipCount> clips;
std::array<std::array<char,64>,bones> names;
std::array<std::int16_t,bones> parents;
struct Graph{RE::BSTSmartPointer<RE::BShkbAnimationGraph> graph;unsigned count{};std::array<unsigned,bones> map;};
struct GraphStamp{const RE::BShkbAnimationGraph* graph;const void* skeleton;std::size_t bones;bool operator==(const GraphStamp&)const=default;};
struct ActorPose{const RE::NiAVObject* root{};std::vector<Graph> graphs;std::vector<GraphStamp> graphStamp;Frame pose{};int gait=0;float time=0;bool wrote=false;bool publish=true;bool bound=false;};
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
std::string OverlayName(std::string_view name){
    if(name.starts_with("x_"))name.remove_prefix(2);
    if(const auto bracket=name.find(" [");bracket!=std::string_view::npos)name=name.substr(0,bracket);
    std::string result;result.reserve(name.size());
    for(const unsigned char c:name)if(std::isalnum(c))result.push_back(static_cast<char>(std::tolower(c)));
    return result;
}
bool OverlayBone(std::string_view name){
    const auto n=OverlayName(name);
    if(n=="npcspine"||n=="npcspine1"||n=="npcspine2"||n=="npcneck"||n=="npchead")return true;
    const bool side=n.starts_with("npcl")||n.starts_with("npcr");
    return side&&(n.find("clavicle")!=std::string::npos||n.find("upperarm")!=std::string::npos||n.find("forearm")!=std::string::npos||
        n.find("hand")!=std::string::npos||n.find("thumb")!=std::string::npos||n.find("index")!=std::string::npos||
        n.find("middle")!=std::string::npos||n.find("ring")!=std::string::npos||n.find("pinkie")!=std::string::npos);
}
void GeneratePose(RE::hkbGenerator* self,const RE::hkbContext& context,const RE::hkbGeneratorOutput** children,RE::hkbGeneratorOutput& output,float offset){
    original(self,context,children,output,offset);
    std::scoped_lock lock(mutex);
    for(auto& [id,actor]:actors)for(const auto& binding:actor.graphs){
        if(&binding.graph->characterInstance!=context.character)continue;
        if(!actor.publish)return;
        if(!output.tracks)return;
        const auto& header=output.tracks->masterHeader;
        if(header.numTracks<3||header.numTracks>128||header.numBytes<16+header.numTracks*16)return;
        const auto& track=output.tracks->trackHeaders[2];
        if((actor.bound?track.numData<15:track.numData<bones)||track.numData>binding.count||track.capacity<track.numData||track.elementSizeBytes!=sizeof(Pose)||
            static_cast<int>(track.flags)!=0||static_cast<int>(track.type)!=1||track.onFraction<=0||
            track.dataOffset<16+header.numTracks*16||track.dataOffset>header.numBytes||
            track.numData*sizeof(Pose)>header.numBytes-track.dataOffset)return;
        auto* data=reinterpret_cast<std::byte*>(output.tracks)+track.dataOffset;
        if(std::any_of(binding.map.begin(),binding.map.end(),[&](unsigned index){return index!=std::numeric_limits<unsigned>::max()&&index>=static_cast<unsigned>(track.numData);}))return;
        for(unsigned i=0;i<bones;++i)if(binding.map[i]!=std::numeric_limits<unsigned>::max()){
            auto* destination=reinterpret_cast<Pose*>(data+binding.map[i]*sizeof(Pose));
            if(actor.bound)std::memcpy(destination->q,actor.pose[i].q,sizeof(destination->q));
            else std::memcpy(destination,&actor.pose[i],sizeof(Pose));
        }
        if(!actor.wrote){actor.wrote=true;spdlog::info("Native crawl output active ref={:08X} tracks={}",id,track.numData);}
        // Only animation output is touched, before skin/SMP publication.
        // Unmapped custom bones and the character controller remain untouched.
        return;
    }
}
bool Available(){return loaded;}
bool Install(const char* path){
    std::ifstream file(path,std::ios::binary);
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
std::vector<GraphStamp> Snapshot(const RE::BSAnimationGraphManager& manager){
    std::vector<GraphStamp> result;result.reserve(manager.graphs.size());
    for(const auto& graph:manager.graphs){
        if(!graph||!graph->characterInstance.setup)continue;
        const auto skeleton=graph->characterInstance.setup->animationSkeleton;
        result.push_back({graph.get(),skeleton.get(),skeleton?static_cast<std::size_t>(skeleton->bones.size()):0u});
    }
    return result;
}
bool Matches(const RE::BSAnimationGraphManager& manager,const std::vector<GraphStamp>& stamp){
    std::size_t index=0;
    for(const auto& graph:manager.graphs){
        if(!graph||!graph->characterInstance.setup)continue;
        const auto skeleton=graph->characterInstance.setup->animationSkeleton;
        if(index>=stamp.size()||stamp[index].graph!=graph.get()||stamp[index].skeleton!=skeleton.get()||
            stamp[index].bones!=(skeleton?static_cast<std::size_t>(skeleton->bones.size()):0u))return false;
        ++index;
    }
    return index==stamp.size();
}
bool BindMode(RE::Actor* actor,bool publish,bool bound){
    if(!actor||!loaded)return false;
    ActorPose result;result.root=actor->Get3D();result.pose=clips[bound?boundClip:0].frames.front();result.gait=bound?boundClip:0;
    result.publish=publish;result.bound=bound;
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if(!result.root||!actor->GetAnimationGraphManager(manager)||!manager)return false;
    result.graphStamp=Snapshot(*manager);
    for(auto& graph:manager->graphs){
        if(!graph||!graph->characterInstance.setup)continue;
        auto skeleton=graph->characterInstance.setup->animationSkeleton;
        if(!skeleton||(!bound&&skeleton->bones.size()<bones)||skeleton->parentIndices.size()!=skeleton->bones.size())continue;
        Graph binding{graph,static_cast<unsigned>(skeleton->bones.size()),{}};binding.map.fill(std::numeric_limits<unsigned>::max());bool valid=true;
        unsigned mapped=0;
        for(unsigned i=0;i<bones&&valid;++i){
            if(bound&&!OverlayBone(names[i].data()))continue;
            const auto wanted=bound?OverlayName(names[i].data()):std::string(Canonical(names[i].data()));bool found=false;
            for(unsigned j=0;j<binding.count;++j)if(skeleton->bones[j].name.c_str()){
                const auto actual=bound?OverlayName(skeleton->bones[j].name.c_str()):std::string(Canonical(skeleton->bones[j].name.c_str()));
                if(actual==wanted){binding.map[i]=j;found=true;++mapped;break;}
            }
            if(!found){valid=false;break;}
            if(!bound){const int expected=parents[i]<0?-1:static_cast<int>(binding.map[parents[i]]);if(skeleton->parentIndices[binding.map[i]]!=expected)valid=false;}
        }
        if(bound&&mapped<15)valid=false;
        if(valid)result.graphs.push_back(std::move(binding));
    }
    if(result.graphs.empty()){
        std::scoped_lock lock(mutex);auto& retry=retries[actor->GetFormID()];
        if(retry.root!=result.root)spdlog::warn("Crawl skeleton incompatible ref={:08X}",actor->GetFormID());
        retry={result.root,1.f};actors.erase(actor->GetFormID());return false;
    }
    std::scoped_lock lock(mutex);retries.erase(actor->GetFormID());actors.insert_or_assign(actor->GetFormID(),std::move(result));return true;
}
bool Bind(RE::Actor* actor,bool publish){return BindMode(actor,publish,false);}
bool BindBound(RE::Actor* actor){return BindMode(actor,true,true);}
bool Resume(RE::Actor* actor){
    std::scoped_lock lock(mutex);auto it=actors.find(actor->GetFormID());
    if(it==actors.end()||it->second.publish)return false;
    it->second.publish=true;return true;
}
void Update(RE::Actor* actor,int gait,float delta){
    if(!actor)return;
    {std::scoped_lock lock(mutex);auto it=retries.find(actor->GetFormID());if(it!=retries.end()&&it->second.root==actor->Get3D()){
        it->second.remaining-=std::clamp(delta,0.f,.1f);if(it->second.remaining>0)return;
    }}
    bool bind=false;{
        RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;actor->GetAnimationGraphManager(manager);
        std::scoped_lock lock(mutex);auto it=actors.find(actor->GetFormID());
        bind=it==actors.end()||it->second.bound||it->second.root!=actor->Get3D()||!manager||!Matches(*manager,it->second.graphStamp);
    }
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
void UpdateBound(RE::Actor* actor,float delta){
    if(!actor)return;
    bool bind=false;{
        RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;actor->GetAnimationGraphManager(manager);
        std::scoped_lock lock(mutex);auto it=actors.find(actor->GetFormID());
        bind=it==actors.end()||!it->second.bound||it->second.root!=actor->Get3D()||!manager||!Matches(*manager,it->second.graphStamp);
    }
    if(bind&&!BindBound(actor))return;
    std::scoped_lock lock(mutex);auto& state=actors.at(actor->GetFormID());state.time=std::fmod(state.time+std::clamp(delta,0.f,.1f),clips[boundClip].duration);
    Frame next;Sample(clips[boundClip],state.time,next);Blend(state.pose,next,1.f-std::exp(-20.f*std::clamp(delta,0.f,.1f)),state.pose);
}
bool Anchor(RE::Actor* actor,RE::NiPoint3& point,float sway){
    if(!actor)return false;
    std::scoped_lock lock(mutex);
    const auto it=actors.find(actor->GetFormID());
    if(it==actors.end()||it->second.root!=actor->Get3D())return false;
    for(unsigned i=0;i<bones;++i)if(std::string_view(names[i].data())=="NPC Spine2 [Spn2]"){
        const auto rest=PosePoint(clips[0].frames.front(),parents,static_cast<int>(i));
        (void)sway;
        // The mount centreline is authoritative. Rider motion is authored by the
        // dedicated Phantom Horse clip in Seat, never by a second sine wave here.
        point={0.f,rest[1],rest[2]};return true;
    }
    return false;
}
bool Working(RE::Actor* actor){std::scoped_lock lock(mutex);auto it=actor?actors.find(actor->GetFormID()):actors.end();return it!=actors.end()&&it->second.wrote;}
void Remove(RE::FormID id){std::scoped_lock lock(mutex);actors.erase(id);retries.erase(id);}
void Clear(){std::scoped_lock lock(mutex);actors.clear();retries.clear();}
}
