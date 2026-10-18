#include "pch.h"
#include "Seat.h"
#include "Crawl.h"
#include <numbers>
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
struct GraphBinding {RE::BSTSmartPointer<RE::BShkbAnimationGraph> graph;unsigned count;std::array<unsigned,bones> map;std::vector<std::int16_t> parents;bool fullRoot=false;};
struct GraphStamp {const RE::BShkbAnimationGraph* graph;const void* skeleton;std::size_t bones;bool operator==(const GraphStamp&)const=default;};
std::vector<GraphBinding> graphs;
std::vector<GraphStamp> graphStamp;
const RE::BSAnimationGraphManager* boundManager=nullptr;
const RE::NiAVObject* boundRoot=nullptr;
bool boundSwimming=false;
const char* bindError="座位资源未加载，请查看NPCMounts日志";
RE::NiPoint3 target,exitTarget;
float frameDelta=0.f;
bool airborne=false,filterValid=false;
const RE::NiAVObject* filteredRoot=nullptr;
float filteredHeight=0.f;
float progress=-1.f;
bool active=false, loaded=false;
bool entering=false;
std::atomic<bool> wrote=false;
std::atomic<unsigned> writes=0;
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
std::string_view Canonical(std::string_view name){
    return name=="x_NPC LookNode [Look]"?"NPC LookNode [Look]":name;
}
bool ResolveTarget(RE::NiPoint3& desired){
    desired=target;
    if(progress>=0){
        const float t=std::clamp(progress,0.f,1.f),blend=t*t*(3-2*t);
        desired=desired*(1-blend)+exitTarget*blend;
    }
    return std::isfinite(desired.x)&&std::isfinite(desired.y)&&std::isfinite(desired.z);
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
    RE::NiPoint3 desired;if(!ResolveTarget(desired))return;
    const float phase=progress;
    if(!output.tracks)return;
    const auto& header=output.tracks->masterHeader;
    if(header.numTracks<3 || header.numTracks>128 || header.numBytes<16+header.numTracks*16)return;
    const auto& track=output.tracks->trackHeaders[2];
    const int bytes=bones*sizeof(Pose),begin=16+header.numTracks*16;
    if(track.numData<bones || track.numData>binding->count || track.capacity<track.numData || track.elementSizeBytes!=sizeof(Pose) ||
        static_cast<int>(track.flags)!=0 || static_cast<int>(track.type)!=1 || track.onFraction<=0 ||
        track.dataOffset<begin || track.dataOffset>header.numBytes || track.numData*sizeof(Pose)>header.numBytes-track.dataOffset)return;
    auto* outputPose=reinterpret_cast<std::byte*>(output.tracks)+track.dataOffset;
    auto* native=reinterpret_cast<Pose*>(outputPose);std::vector<Pose> pose(track.numData);std::memcpy(pose.data(),outputPose,track.numData*sizeof(Pose));
    // Keep native clip timing/extracted motion. Only the rendered human pose
    // is replaced; locomotion must not be replaced by a nine-second idle clip.
    std::array<Pose,bones> source;if(phase>=0)Sample(phase,source.data());else source=animation.front();
    // The rider uses the static riding root/helper tracks. Native locomotion
    // root blending leaves a small lateral drift, especially while running.
    // Extracted motion is a separate output track and still drives physics;
    // camera tracks remain native.
    for(unsigned i=0;i<bones;++i)if(binding->map[i]!=std::numeric_limits<unsigned>::max()&&
        !std::string_view(names[i].data()).starts_with("Camera")&&(binding->fullRoot||i>=4))pose[binding->map[i]]=source[i];
    // Rider body is authored below; extracted motion still drives physics.
    std::vector<RE::NiPoint3> positions(track.numData);std::vector<RE::NiMatrix3> basis(track.numData);
    for(unsigned i=0;i<static_cast<unsigned>(track.numData);++i){
        auto r=Rotation(pose[i].q);for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)r.entry[row][col]*=pose[i].s[col];
        const RE::NiPoint3 point{pose[i].t[0],pose[i].t[1],pose[i].t[2]};const int parent=binding->parents[i];
        positions[i]=parent<0?point:positions[parent]+basis[parent]*point;basis[i]=parent<0?r:basis[parent]*r;
    }
    // Only local animation output is changed, before skeleton/SMP publication.
    // Pelvis=5, COM=4 and the 99-bone prefix are verified in Bind().
    // Extended XPMSSE tracks are never overwritten, including during dismount.
    RE::NiMatrix3 inverse;
    const auto com=binding->map[4],pelvis=binding->map[5];if(com>=pose.size()||pelvis>=pose.size())return;
    const auto comParent=binding->parents[com];if(comParent<0||!Inverse(basis[comParent],inverse))return;
    const auto correction=inverse*(desired-positions[pelvis]);
    pose[com].t[0]+=correction.x;pose[com].t[1]+=correction.y;pose[com].t[2]+=correction.z;
    for(unsigned i=0;i<bones;++i)if(binding->map[i]!=std::numeric_limits<unsigned>::max()&&
        !std::string_view(names[i].data()).starts_with("Camera")&&(binding->fullRoot||i>=4))native[binding->map[i]]=pose[binding->map[i]];
    wrote.store(true,std::memory_order_relaxed);
    writes.fetch_add(1,std::memory_order_relaxed);
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
std::string BoneKey(std::string_view name){
    if(name.starts_with("x_"))name.remove_prefix(2);if(const auto bracket=name.find(" [");bracket!=std::string_view::npos)name=name.substr(0,bracket);
    std::string result;for(const unsigned char c:name)if(std::isalnum(c))result.push_back(static_cast<char>(std::tolower(c)));return result;
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
std::vector<GraphBinding> Collect(RE::Actor* player){
    std::vector<GraphBinding> result;
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
    if(!player->GetAnimationGraphManager(manager)||!manager)return result;
    bindError="玩家动画骨架的基础骨骼不兼容，详见NPCMounts日志";
    for(auto& graph:manager->graphs){
        if(!graph||!graph->characterInstance.setup)continue;
        auto skeleton=graph->characterInstance.setup->animationSkeleton;
        if(!skeleton||skeleton->bones.size()<bones||skeleton->parentIndices.size()!=skeleton->bones.size()){
            spdlog::warn("Seat bind rejected: bones={} parents={}",skeleton?skeleton->bones.size():0,skeleton?skeleton->parentIndices.size():0);continue;
        }
        GraphBinding binding{graph,static_cast<unsigned>(skeleton->bones.size()),{}, {},false};binding.map.fill(std::numeric_limits<unsigned>::max());binding.parents.assign(skeleton->parentIndices.begin(),skeleton->parentIndices.end());
        bool exact=true;for(unsigned i=0;i<bones;++i)if(!skeleton->bones[i].name.c_str()||Canonical(skeleton->bones[i].name.c_str())!=Canonical(names[i].data())||skeleton->parentIndices[i]!=parents[i]){exact=false;break;}
        if(exact){for(unsigned i=0;i<bones;++i)binding.map[i]=i;binding.fullRoot=true;}
        else{
            bool match=true;for(unsigned i=4;i<bones&&match;++i){const auto wanted=BoneKey(names[i].data());
                for(unsigned j=0;j<binding.count;++j)if(skeleton->bones[j].name.c_str()&&BoneKey(skeleton->bones[j].name.c_str())==wanted){binding.map[i]=j;break;}
                if(binding.map[i]==std::numeric_limits<unsigned>::max())match=false;
                else if(parents[i]>=4&&skeleton->parentIndices[binding.map[i]]!=static_cast<int>(binding.map[parents[i]]))match=false;
            }if(!match)continue;
        }
        result.push_back(std::move(binding));spdlog::info("Seat bind accepted: {} bones, mode={}",skeleton->bones.size(),exact?"full":"mapped-body");
    }
    return result;
}
bool Bind(RE::Actor* player){
    Clear();if(!loaded){bindError="座位资源未加载，请查看NPCMounts日志";spdlog::warn("Seat bind: animation resource not loaded");return false;}
    auto next=Collect(player);if(next.empty()){bindError="玩家动画图尚未加载或基础骨骼不兼容，详见NPCMounts日志";return false;}
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;player->GetAnimationGraphManager(manager);
    auto stamp=Snapshot(*manager);
    std::scoped_lock lock(mutex);graphs=std::move(next);graphStamp=std::move(stamp);boundManager=manager.get();boundRoot=player->Get3D();boundSwimming=player->AsActorState()->IsSwimming();return true;
}
bool Refresh(RE::Actor* player){
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;if(!player||!player->GetAnimationGraphManager(manager)||!manager)return false;
    {std::scoped_lock lock(mutex);if(boundManager==manager.get()&&boundRoot==player->Get3D()&&Matches(*manager,graphStamp))return !graphs.empty();}
    auto next=Collect(player);if(next.empty())return false;
    auto stamp=Snapshot(*manager);
    std::scoped_lock lock(mutex);graphs=std::move(next);graphStamp=std::move(stamp);boundManager=manager.get();boundRoot=player->Get3D();boundSwimming=player->AsActorState()->IsSwimming();active=active&&!graphs.empty();wrote=false;return true;
}
const char* BindError(){return bindError;}
void Clear(){std::scoped_lock lock(mutex);active=false;graphs.clear();graphStamp.clear();boundManager=nullptr;boundRoot=nullptr;boundSwimming=false;wrote=false;finished=false;progress=-1.f;entering=false;filterValid=false;filteredRoot=nullptr;frameDelta=0.f;}
void Motion(float delta,bool air){std::scoped_lock lock(mutex);frameDelta=std::clamp(delta,0.f,.1f);airborne=air;}
void Entering(bool value){std::scoped_lock lock(mutex);entering=value;finished=false;}
bool Working(){return wrote.load(std::memory_order_relaxed);}
unsigned TakeWrites(){return writes.exchange(0,std::memory_order_relaxed);}
bool Finished(){return finished.load(std::memory_order_acquire);}
void Update(RE::Actor* player,RE::Actor* mount,const Settings::Data& config,float phase,RE::NiPoint3 exitOffset){
    // Invalid/unloaded geometry must not leave the previous target active.
    const auto suspend=[](){std::scoped_lock lock(mutex);active=false;wrote=false;finished=false;filterValid=false;};
    auto* p=player->Get3D();auto* m=mount->Get3D();
    if(!p||!m||!std::isfinite(p->world.scale)||!std::isfinite(m->world.scale)||p->world.scale<=0||m->world.scale<=0){suspend();return;}
    auto* back=m->GetObjectByName("SaddleBone");if(!back)back=m->GetObjectByName("NPC Spine2 [Spn2]");if(!back){suspend();return;}
    RE::NiPoint3 local;
    if(!Crawl::Anchor(mount,local,config.riderSway)){suspend();return;}
    // A real custom saddle keeps its model-space offset; ordinary spine uses the
    // same sampled pose as Crawl, not last frame's world-space skeleton.
    if(m->GetObjectByName("SaddleBone"))local=m->world.rotate.Transpose()*(back->world.translate-m->world.translate)/m->world.scale;
    local=local+RE::NiPoint3{config.seatSide,config.seatForward,config.seatHeight};
    auto desired=local;
    // Filter only pose-local height. World translation/terrain must not enter this filter.
    {
        std::scoped_lock lock(mutex);
        if(phase<0&&!airborne&&frameDelta>0.f&&filterValid&&filteredRoot==m&&std::abs(desired.z-filteredHeight)<12.f)
            desired.z=filteredHeight+(desired.z-filteredHeight)*(1.f-std::exp(-18.f*frameDelta));
        filteredHeight=desired.z;filteredRoot=m;filterValid=phase<0;
    }
    if(!std::isfinite(desired.x)||!std::isfinite(desired.y)||!std::isfinite(desired.z)){suspend();return;}
    const auto playerWorld=p->world,mountWorld=m->world;
    desired=playerWorld.rotate.Transpose()*(mountWorld.translate-playerWorld.translate)/playerWorld.scale+
        playerWorld.rotate.Transpose()*mountWorld.rotate*desired*(mountWorld.scale/playerWorld.scale);
    std::array<RE::NiPoint3,bones> end;std::array<RE::NiMatrix3,bones> endBasis;World(animation.back().data(),end,endBasis);
    const auto exitWorld=player->GetPosition()+exitOffset;
    const auto exit=end[5]+playerWorld.rotate.Transpose()*(exitWorld-playerWorld.translate)/playerWorld.scale;
    std::scoped_lock lock(mutex);target=desired;exitTarget=exit;progress=phase;active=!graphs.empty();
}
}
