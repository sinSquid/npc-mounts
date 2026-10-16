#include "pch.h"
#include "Seat.h"
#include <array>
#include <atomic>
#include <fstream>
#include <mutex>

namespace Seat {
constexpr unsigned bones=99, frames=31;
struct Pose { float t[4], q[4], s[4]; };
static_assert(sizeof(Pose)==sizeof(RE::hkQsTransform));
std::array<std::array<Pose,bones>,frames> animation;
std::array<std::array<char,64>,bones> names;
std::array<std::int16_t,bones> parents;
std::mutex mutex;
struct GraphBinding {RE::BSTSmartPointer<RE::BShkbAnimationGraph> graph;unsigned count;};
std::vector<GraphBinding> graphs;
const char* bindError="座位资源未加载，请查看NPCMounts日志";
RE::NiPoint3 target;
float progress=-1.f;
bool active=false, loaded=false;
bool entering=false;
std::atomic<bool> wrote=false;
std::atomic<bool> finished=false;
using Generate=void(*)(RE::hkbGenerator*,const RE::hkbContext&,const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float);
Generate original{};

RE::NiMatrix3 Rotation(const float* q) {
    const float x=q[0],y=q[1],z=q[2],w=q[3]; RE::NiMatrix3 r;
    r.entry[0][0]=1-2*(y*y+z*z); r.entry[0][1]=2*(x*y-z*w); r.entry[0][2]=2*(x*z+y*w);
    r.entry[1][0]=2*(x*y+z*w); r.entry[1][1]=1-2*(x*x+z*z); r.entry[1][2]=2*(y*z-x*w);
    r.entry[2][0]=2*(x*z-y*w); r.entry[2][1]=2*(y*z+x*w); r.entry[2][2]=1-2*(x*x+y*y); return r;
}
void World(const Pose* pose,std::array<RE::NiPoint3,bones>& positions,std::array<RE::NiMatrix3,bones>& basis) {
    for(unsigned i=0;i<bones;++i) {
        auto r=Rotation(pose[i].q);
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)r.entry[row][col]*=pose[i].s[col];
        const RE::NiPoint3 p{pose[i].t[0],pose[i].t[1],pose[i].t[2]}; const int parent=parents[i];
        positions[i]=parent<0?p:positions[parent]+basis[parent]*p;
        basis[i]=parent<0?r:basis[parent]*r;
    }
}
void Sample(float t,Pose* pose) {
    float f=std::clamp(t,0.f,1.f)*(frames-1);unsigned a=static_cast<unsigned>(f),b=std::min(a+1,frames-1);float weight=f-a;
    for(unsigned i=0;i<bones;++i){
        const auto& p=animation[a][i]; const auto& q=animation[b][i];
        float dot=0;for(unsigned n=0;n<4;++n)dot+=p.q[n]*q.q[n];float norm=0;
        for(unsigned n=0;n<4;++n){
            pose[i].t[n]=p.t[n]+weight*(q.t[n]-p.t[n]);pose[i].s[n]=p.s[n]+weight*(q.s[n]-p.s[n]);
            pose[i].q[n]=p.q[n]+weight*((dot<0?-q.q[n]:q.q[n])-p.q[n]);norm+=pose[i].q[n]*pose[i].q[n];
        }
        for(float& v:pose[i].q)v/=std::sqrt(norm);
    }
}
bool Inverse(const RE::NiMatrix3& matrix,RE::NiMatrix3& inverse) {
    const auto& a=matrix.entry;auto& b=inverse.entry;
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)
        b[col][row]=a[(row+1)%3][(col+1)%3]*a[(row+2)%3][(col+2)%3]-a[(row+1)%3][(col+2)%3]*a[(row+2)%3][(col+1)%3];
    const float determinant=a[0][0]*b[0][0]+a[0][1]*b[1][0]+a[0][2]*b[2][0];
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-8f)return false;
    for(auto& row:b)for(float& v:row)v/=determinant;return true;
}
void GeneratePose(RE::hkbGenerator* self,const RE::hkbContext& context,const RE::hkbGeneratorOutput** children,RE::hkbGeneratorOutput& output,float offset) {
    original(self,context,children,output,offset);
    // Serialize output with Clear(): an in-flight job must not publish another
    // riding pose after dismount cleanup has returned.
    std::scoped_lock lock(mutex);
    if(!active)return;
    const auto binding=std::find_if(graphs.begin(),graphs.end(),[&](const auto& g){return &g.graph->characterInstance==context.character;});
    if(binding==graphs.end())return;
    const auto desired=target;const float phase=progress;
    if(!output.tracks)return;
    const auto& header=output.tracks->masterHeader;
    if(header.numTracks<3 || header.numTracks>128 || header.numBytes<16+header.numTracks*16)return;
    const auto& track=output.tracks->trackHeaders[2];
    const int bytes=bones*sizeof(Pose),begin=16+header.numTracks*16;
    if(track.numData<bones || track.numData>binding->count || track.capacity<track.numData || track.elementSizeBytes!=sizeof(Pose) ||
        static_cast<int>(track.flags)!=0 || static_cast<int>(track.type)!=1 || track.onFraction<=0 ||
        track.dataOffset<begin || track.dataOffset>header.numBytes || track.numData*sizeof(Pose)>header.numBytes-track.dataOffset)return;
    auto* outputPose=reinterpret_cast<std::byte*>(output.tracks)+track.dataOffset;
    std::array<Pose,bones> pose;std::memcpy(pose.data(),outputPose,bytes);
    // Keep native clip timing/extracted motion. Only the rendered human pose
    // is replaced; locomotion must not be replaced by a nine-second idle clip.
    auto nativePose=pose;
    if(phase>=0)Sample(phase,pose.data());else pose=animation.front();
    for(unsigned i=0;i<bones;++i)
        if(i<4||std::string_view(names[i].data()).starts_with("Camera"))pose[i]=nativePose[i];
    std::array<RE::NiPoint3,bones> positions;std::array<RE::NiMatrix3,bones> basis;
    World(pose.data(),positions,basis);
    // Only local animation output is changed, before skeleton/SMP publication.
    // Pelvis=5, COM=4 and the 99-bone prefix are verified in Bind().
    // Extended XPMSSE tracks are never overwritten, including during dismount.
    RE::NiMatrix3 inverse;
    if(!Inverse(basis[parents[4]],inverse))return;
    const auto correction=inverse*(desired-positions[5]);
    pose[4].t[0]+=correction.x;pose[4].t[1]+=correction.y;pose[4].t[2]+=correction.z;
    std::memcpy(outputPose,pose.data(),bytes);
    wrote.store(true,std::memory_order_relaxed);
    if(entering?phase==0.f:phase>=1.f)finished.store(true,std::memory_order_release);
}
bool Install(){
    std::ifstream file("Data/SKSE/Plugins/NPCMounts/Dismount.nmp",std::ios::binary);
    std::array<std::uint32_t,4> header{};file.read(reinterpret_cast<char*>(header.data()),sizeof(header));
    if(header!=std::array<std::uint32_t,4>{0x504d504e,1,bones,frames})return false;
    file.read(reinterpret_cast<char*>(names.data()),sizeof(names));file.read(reinterpret_cast<char*>(parents.data()),sizeof(parents));
    file.read(reinterpret_cast<char*>(animation.data()),sizeof(animation));if(!file || file.peek()!=EOF)return false;
    for(unsigned i=0;i<bones;++i)if(parents[i]>=static_cast<int>(i)||parents[i]<-1||names[i][63]!=0)return false;
    if(std::string_view(names[4].data())!="NPC COM [COM ]"||std::string_view(names[5].data())!="NPC Pelvis [Pelv]"||parents[4]<0)return false;
    for(const auto& frame:animation)for(const auto& p:frame){
        for(float v:p.t)if(!std::isfinite(v))return false;
        float norm=0;for(float v:p.q){if(!std::isfinite(v))return false;norm+=v*v;}
        if(std::abs(norm-1)>0.01f)return false;
        for(unsigned j=0;j<3;++j)if(!std::isfinite(p.s[j])||p.s[j]<=0)return false;
    }
    REL::Relocation<std::uintptr_t> graph{RE::VTABLE_hkbBehaviorGraph[0]};
    original=reinterpret_cast<Generate>(graph.write_vfunc(0x17,GeneratePose));loaded=true;return true;
}
bool Bind(RE::Actor* player){
    Clear();if(!loaded){bindError="座位资源未加载，请查看NPCMounts日志";spdlog::warn("Seat bind: animation resource not loaded");return false;}
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if(!player->GetAnimationGraphManager(manager)||!manager){bindError="玩家动画图尚未加载，请切换第三人称后重试";spdlog::warn("Seat bind: no animation graph manager");return false;}
    bindError="玩家动画骨架的基础骨骼不兼容，详见NPCMounts日志";
    std::scoped_lock lock(mutex);
    for(auto& graph:manager->graphs){
        if(!graph||!graph->characterInstance.setup)continue;
        auto skeleton=graph->characterInstance.setup->animationSkeleton;
        if(!skeleton||skeleton->bones.size()<bones||skeleton->parentIndices.size()!=skeleton->bones.size()){
            spdlog::warn("Seat bind rejected: bones={} parents={}",skeleton?skeleton->bones.size():0,skeleton?skeleton->parentIndices.size():0);continue;
        }
        bool match=true;for(unsigned i=0;i<bones;++i)
            if(!skeleton->bones[i].name.c_str()||std::strcmp(skeleton->bones[i].name.c_str(),names[i].data())||skeleton->parentIndices[i]!=parents[i]){
                spdlog::warn("Seat bind rejected: index={} actual={} expected={} parent={}/{}",i,skeleton->bones[i].name.c_str()?skeleton->bones[i].name.c_str():"<null>",names[i].data(),skeleton->parentIndices[i],parents[i]);match=false;break;
            }
        if(match){graphs.push_back({graph,static_cast<unsigned>(skeleton->bones.size())});spdlog::info("Seat bind accepted: {} bones, compatible {}-bone prefix",skeleton->bones.size(),bones);}
    }
    return !graphs.empty();
}
const char* BindError(){return bindError;}
void Clear(){std::scoped_lock lock(mutex);active=false;graphs.clear();wrote=false;finished=false;progress=-1.f;entering=false;}
void Entering(bool value){std::scoped_lock lock(mutex);entering=value;finished=false;}
bool Working(){return wrote.load(std::memory_order_relaxed);}
bool Finished(){return finished.load(std::memory_order_acquire);}
void Update(RE::Actor* player,RE::Actor* mount,const Settings::Data& config,float phase,RE::NiPoint3 exitOffset){
    // Invalid/unloaded geometry must not leave the previous target active.
    const auto suspend=[](){std::scoped_lock lock(mutex);active=false;wrote=false;finished=false;};
    auto* p=player->Get3D();auto* m=mount->Get3D();
    if(!p||!m||!std::isfinite(p->world.scale)||!std::isfinite(m->world.scale)||p->world.scale<=0||m->world.scale<=0){suspend();return;}
    auto* back=m->GetObjectByName("SaddleBone");if(!back)back=m->GetObjectByName("NPC Spine2 [Spn2]");if(!back){suspend();return;}
    // Anchor to the published back, not a hypothetical back at the reference
    // position. Physics/reference and rendered roots can differ while moving.
    const auto offset=m->world.rotate*RE::NiPoint3{config.seatSide,config.seatForward,config.seatHeight}*m->world.scale;
    auto world=back->world.translate-p->world.translate+offset;
    auto desired=p->world.rotate.Transpose()*world/p->world.scale;
    if(phase>=0){
        std::array<RE::NiPoint3,bones> end;std::array<RE::NiMatrix3,bones> basis;World(animation.back().data(),end,basis);
        const float t=std::clamp(phase,0.f,1.f),blend=t*t*(3-2*t);
        const auto endPosition=end[5]+p->world.rotate.Transpose()*(player->GetPosition()+exitOffset-p->world.translate)/p->world.scale;
        desired=desired*(1-blend)+endPosition*blend;
    }
    if(!std::isfinite(desired.x)||!std::isfinite(desired.y)||!std::isfinite(desired.z)){suspend();return;}
    std::scoped_lock lock(mutex);target=desired;progress=phase;active=!graphs.empty();
}
}
