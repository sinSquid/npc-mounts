// Execute production sampling, binding and output against the actual packaged motion bank.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),s=fs.readFileSync(path.join(root,'src/Crawl.cpp'),'utf8');
function take(sig){let start=s.indexOf(sig);assert(start>=0,sig);let end=s.indexOf('{',start)+1,n=1;while(n){const c=s[end++];if(c==='{')n++;if(c==='}')n--;}return s.slice(start,end);}
const globals=s.slice(s.indexOf('constexpr unsigned bones'),s.indexOf('using Generate='));
fs.writeFileSync(path.join(root,'build/crawl-regression.cpp'),`
#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <iterator>
#include <cctype>
#include <limits>
#include "../src/PosePoint.h"
namespace RE{
struct NiPoint3{float x,y,z;};
using FormID=unsigned;template<class T>using BSTSmartPointer=std::shared_ptr<T>;struct NiAVObject{};
struct Bone{std::string name;};struct Skeleton{std::vector<Bone>bones;std::vector<int>parentIndices;};struct Setup{std::shared_ptr<Skeleton>animationSkeleton;};struct Character{std::shared_ptr<Setup>setup;};struct BShkbAnimationGraph{Character characterInstance;};struct BSAnimationGraphManager{std::vector<std::shared_ptr<BShkbAnimationGraph>>graphs;};
struct Actor{unsigned id=1;NiAVObject root;std::shared_ptr<BSAnimationGraphManager>manager=std::make_shared<BSAnimationGraphManager>();unsigned GetFormID(){return id;}NiAVObject* Get3D(){return &root;}bool GetAnimationGraphManager(std::shared_ptr<BSAnimationGraphManager>& out){out=manager;return true;}};
struct hkbGenerator{};struct hkbContext{Character* character;};struct hkbGeneratorOutput{struct Master{int numBytes,numTracks;std::uint8_t unused[8];};struct Header{std::int16_t capacity,numData,dataOffset,elementSizeBytes;float onFraction;std::int8_t flags,type;std::uint16_t pad;};struct Tracks{Master masterHeader;Header trackHeaders[1];};Tracks* tracks;};}
namespace spdlog{template<class...T>void warn(const char*,T...){}template<class...T>void info(const char*,T...){}}
${globals}
void nativeOriginal(RE::hkbGenerator*,const RE::hkbContext&,const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float){}
using Generate=decltype(&nativeOriginal);Generate original{};int hookWrites=0;
namespace RE{constexpr std::uintptr_t VTABLE_hkbBehaviorGraph[]{1};}
namespace REL{template<class T>struct Relocation{Relocation(T){}template<class F>std::uintptr_t write_vfunc(int slot,F){assert(slot==0x17);++hookWrites;return reinterpret_cast<std::uintptr_t>(&nativeOriginal);}};}
bool Bind(RE::Actor*,bool publish=true);
${['void Blend(','void Sample(','std::string_view Canonical(','std::string OverlayName(','bool OverlayBone(','void GeneratePose(','bool Available(','bool Install(','std::vector<GraphStamp> Snapshot(','bool Matches(','bool BindMode(','bool Bind(','bool BindBound(','bool Resume(','void Update(','void UpdateBound(','bool Anchor(','bool Working(','void Remove(','void Clear('].map(take).join('\n')}
int main(int argc,char** argv){assert(argc==2||argc==3);
 if(argc==3){assert(!Install(argv[2])&&!Available()&&hookWrites==0);std::puts("PASS: original failing J bank rejected by production Install.");}
 std::ifstream f(argv[1],std::ios::binary);std::string good((std::istreambuf_iterator<char>(f)),{});assert(!good.empty());
 // Exercise the exact production Install, including file validation and hook gate.
 // Only the engine vtable write is mocked; no test sets loaded=true.
 for(int kind=0;kind<7;++kind){auto bad=good;const size_t scale=16+99*64+99*2+8+32;
  if(kind==0)bad[0]=0;
  if(kind==1)bad.resize(16);
  if(kind==2)bad.push_back('x');
  if(kind==3){float negative=-3445.288f;memcpy(bad.data()+scale,&negative,4);}
  if(kind==4){float zero=0;memcpy(bad.data()+scale,&zero,4);}
  if(kind==5){std::uint32_t nan=0x7fc00000;memcpy(bad.data()+scale,&nan,4);}
  if(kind==6){std::int16_t parent=99;memcpy(bad.data()+16+99*64,&parent,2);}
  {std::ofstream out("build/crawl-invalid-test.nmc",std::ios::binary);out.write(bad.data(),bad.size());}
  assert(!Install("build/crawl-invalid-test.nmc"));assert(!Available()&&hookWrites==0);
 }
 assert(!Install("build/nonexistent-crawl-test.nmc")&&!Available()&&hookWrites==0);
 assert(Install(argv[1])&&Available()&&hookWrites==1);
 std::puts("PASS: production Install rejects missing/truncated/header/trailing/parent/negative/zero/NaN data before hook, accepts candidate bank.");
 for(auto& c:clips){Frame a,b;Sample(c,c.duration*.2f,a);Sample(c,c.duration*.7f,b);assert(memcmp(&a,&b,sizeof(Frame))!=0);for(const auto& p:a){float norm=0;for(float v:p.q){assert(std::isfinite(v));norm+=v*v;}assert(std::abs(norm-1)<.0001f);}}
 for(unsigned count:{99u,126u}){
 RE::Actor a;auto g=std::make_shared<RE::BShkbAnimationGraph>();g->characterInstance.setup=std::make_shared<RE::Setup>();auto sk=std::make_shared<RE::Skeleton>();g->characterInstance.setup->animationSkeleton=sk;a.manager->graphs.push_back(g);
 for(unsigned i=0;i<count;i++){sk->bones.push_back({i<99?names[i].data():"Custom"+std::to_string(i)});sk->parentIndices.push_back(i<99?parents[i]:0);}
 for(auto& bone:sk->bones)if(bone.name=="NPC LookNode [Look]")bone.name="x_NPC LookNode [Look]";
 assert(Bind(&a));actors.at(1).gait=1;actors.at(1).time=.25f;Update(&a,1,0);assert(std::abs(actors.at(1).time-.25f)<1e-6f);
 auto water=std::make_shared<RE::BShkbAnimationGraph>();water->characterInstance.setup=g->characterInstance.setup;a.manager->graphs.push_back(water);Update(&a,1,0);assert(actors.at(1).graphs.size()==2&&actors.at(1).graphStamp.size()==2);a.manager->graphs.erase(a.manager->graphs.begin());Update(&a,1,0);assert(actors.at(1).graphs.size()==1&&actors.at(1).graphStamp.size()==1);g=water;
 for(float fps:{30.f,60.f,144.f})for(int gait=0;gait<7;gait++){for(int i=0;i<int(fps*2);i++)Update(&a,gait,1/fps);for(auto& p:actors.at(1).pose){float norm=0;for(float v:p.q){assert(std::isfinite(v));norm+=v*v;}assert(std::abs(norm-1)<.0001f);}}
 for(int old:{1,2,3})for(int next:{1,2,3}){auto& st=actors.at(1);st.gait=old;st.time=clips[old].duration*.37f;Update(&a,next,0);assert(std::abs(st.time/clips[next].duration-.37f)<.00001f);}
 Update(&a,0,0);assert(actors.at(1).time==0);Update(&a,1,0);assert(actors.at(1).time==0);Update(&a,4,0);assert(actors.at(1).time==0);
 RE::NiPoint3 anchor;assert(Anchor(&a,anchor,0));unsigned spine=0;for(unsigned i=0;i<bones;++i)if(std::string_view(names[i].data())=="NPC Spine2 [Spn2]")spine=i;
 auto rest=PosePoint(clips[0].frames.front(),parents,spine);
 assert(std::abs(anchor.x)<1e-5&&std::abs(anchor.y-rest[1])<1e-5&&std::abs(anchor.z-rest[2])<1e-5);
 for(int gait=0;gait<7;++gait){
  actors.at(1).gait=gait;actors.at(1).time=clips[gait].duration*.25f;assert(Anchor(&a,anchor,1));
  assert(std::abs(anchor.x)<1e-5&&std::abs(anchor.y-rest[1])<1e-5&&std::abs(anchor.z-rest[2])<1e-5);
  actors.at(1).time=clips[gait].duration*.75f;assert(Anchor(&a,anchor,.5f));assert(std::abs(anchor.z-rest[2])<1e-5);
 }
 const unsigned header=64,bytes=header+count*sizeof(Pose);std::vector<std::byte>b(bytes,std::byte{0x33});auto* t=(RE::hkbGeneratorOutput::Tracks*)b.data();t->masterHeader={int(bytes),3,{}};auto& h=t->trackHeaders[2];h={short(count),short(count),64,48,1,0,1,0};RE::hkbGeneratorOutput out{t};RE::hkbContext ctx{&g->characterInstance};auto before=b;GeneratePose(nullptr,ctx,nullptr,out,0);assert(memcmp(b.data()+64,actors.at(1).pose.data(),sizeof(Frame))==0);assert(memcmp(b.data()+64+sizeof(Frame),before.data()+64+sizeof(Frame),(count-99)*sizeof(Pose))==0);
 assert(Bind(&a,false));before=b;GeneratePose(nullptr,ctx,nullptr,out,0);assert(b==before&&!actors.at(1).publish);
 Resume(&a);assert(actors.at(1).publish);GeneratePose(nullptr,ctx,nullptr,out,0);assert(memcmp(b.data()+64,actors.at(1).pose.data(),sizeof(Frame))==0);
 for(int bad=0;bad<5;bad++){h={short(count),short(count),64,48,1,0,1,0};if(bad==0)h.numData=98;if(bad==1)h.flags=1;if(bad==2)h.elementSizeBytes=32;if(bad==3)h.dataOffset=63;if(bad==4)h.type=0;before=b;Update(&a,2,.1f);GeneratePose(nullptr,ctx,nullptr,out,0);assert(b==before);}
 auto name=sk->bones[5].name;sk->bones[5].name="unsupported";assert(!Bind(&a)&&actors.empty());for(int i=0;i<10;i++)Update(&a,0,.01f);assert(actors.empty());sk->bones[5].name=name;assert(Bind(&a));int parent=sk->parentIndices[5];sk->parentIndices[5]=98;assert(!Bind(&a));sk->parentIndices[5]=parent;assert(Bind(&a));Remove(1);assert(actors.empty()&&retries.empty());}
 {RE::Actor giant;giant.id=2;auto g=std::make_shared<RE::BShkbAnimationGraph>();g->characterInstance.setup=std::make_shared<RE::Setup>();auto sk=std::make_shared<RE::Skeleton>();g->characterInstance.setup->animationSkeleton=sk;giant.manager->graphs.push_back(g);
  for(unsigned i=0;i<bones;++i)if(OverlayBone(names[i].data())){sk->bones.push_back({names[i].data()});sk->parentIndices.push_back(sk->parentIndices.empty()?-1:0);}
  assert(sk->bones.size()>=15&&sk->bones.size()<99);assert(BindBound(&giant));UpdateBound(&giant,.016f);assert(actors.at(2).bound&&actors.at(2).gait==int(boundClip));
  const unsigned count=static_cast<unsigned>(sk->bones.size()),header=64,bytes=header+count*sizeof(Pose);std::vector<std::byte>b(bytes);auto* t=(RE::hkbGeneratorOutput::Tracks*)b.data();t->masterHeader={int(bytes),3,{}};auto& h=t->trackHeaders[2];h={short(count),short(count),64,48,1,0,1,0};RE::hkbGeneratorOutput out{t};RE::hkbContext ctx{&g->characterInstance};GeneratePose(nullptr,ctx,nullptr,out,0);assert(actors.at(2).wrote);Remove(2);}
 Clear();assert(actors.empty()&&retries.empty());std::puts("PASS: real 8-clip bank; stable centreline anchor without duplicate rider bob, production 99/126-bone binding, helper alias, parent/name rejection, 30/60/144 FPS sampling, output bounds, extra bones preserved and retry/removal.");}
`);
