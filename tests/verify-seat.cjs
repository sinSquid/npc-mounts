// Actual pose math/output function with dense-buffer test doubles and real samples.
const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(process.argv[2]||path.join(repo,'src/Seat.cpp'),'utf8');
function extract(sig){const start=source.indexOf(sig);assert(start>=0);let end=source.indexOf('{',start)+1,depth=1;while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}return source.slice(start,end);}
fs.writeFileSync(path.join(repo,'build/seat-regression.cpp'),`
#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>
#include <cstring>
#include <string_view>
#include <fstream>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include "../src/Config.h"
#include "../src/PosePoint.h"
#include "../src/PoseAudit.h"
PoseAudit audit;
namespace RE {
struct NiPoint3{float x{},y{},z{};NiPoint3 operator+(NiPoint3 b)const{return{x+b.x,y+b.y,z+b.z};}NiPoint3 operator-(NiPoint3 b)const{return{x-b.x,y-b.y,z-b.z};}NiPoint3 operator*(float k)const{return{x*k,y*k,z*k};}NiPoint3 operator/(float k)const{return{x/k,y/k,z/k};}};
struct NiMatrix3 {float entry[3][3]{{1,0,0},{0,1,0},{0,0,1}};
 NiMatrix3 Transpose()const{NiMatrix3 r;for(int i=0;i<3;i++)for(int j=0;j<3;j++)r.entry[i][j]=entry[j][i];return r;}
 NiPoint3 operator*(NiPoint3 p)const{return{entry[0][0]*p.x+entry[0][1]*p.y+entry[0][2]*p.z,entry[1][0]*p.x+entry[1][1]*p.y+entry[1][2]*p.z,entry[2][0]*p.x+entry[2][1]*p.y+entry[2][2]*p.z};}
 NiMatrix3 operator*(const NiMatrix3& b)const{NiMatrix3 r;for(int i=0;i<3;i++)for(int j=0;j<3;j++){r.entry[i][j]=0;for(int k=0;k<3;k++)r.entry[i][j]+=entry[i][k]*b.entry[k][j];}return r;}
};
struct Node {struct {NiPoint3 translate;NiMatrix3 rotate;float scale=1;}world;Node* saddle=nullptr;Node* spine=nullptr;
 Node* GetObjectByName(const char* name){return std::strcmp(name,"SaddleBone")==0?saddle:spine;}};
using NiAVObject=Node;
template<class T>struct NiPointer{T* p=nullptr;NiPointer& operator=(T* v){p=v;return *this;}T* operator->(){return p;}explicit operator bool()const{return p;}void reset(T* v=nullptr){p=v;}};
struct Actor{Node* node;NiPoint3 position;Node* Get3D(){return node;}NiPoint3 GetPosition(){return position;}};
struct hkbGenerator{};struct hkbCharacter{};struct hkbContext{hkbCharacter* character;};
struct BShkbAnimationGraph{hkbCharacter characterInstance;};
struct BSAnimationGraphManager{};
struct hkbGeneratorOutput{
 struct Master{int numBytes,numTracks;std::uint8_t unused[8];};
 struct Header{std::int16_t capacity,numData,dataOffset,elementSizeBytes;float onFraction;std::int8_t flags,type;std::uint16_t pad;};
 struct Tracks{Master masterHeader;Header trackHeaders[1];};Tracks* tracks;
};
}
namespace Crawl{RE::NiPoint3 sample{4,-18,42};bool Anchor(RE::Actor*,RE::NiPoint3& p,float){p=sample;return true;}}
constexpr unsigned bones=99,frames=31,riderFrames=13;
struct Pose{float t[4],q[4],s[4];};
std::array<std::array<Pose,bones>,frames> animation;
std::array<std::array<Pose,bones>,riderFrames> riderAnimation;
std::array<std::array<char,64>,bones> names;std::array<std::int16_t,bones> parents;
RE::NiPoint3 riderCenter;float riderDuration=0,riderTime=0,riderSway=0,riderBlend=0;int riderGait=0;
struct GraphBinding{std::shared_ptr<RE::BShkbAnimationGraph> graph;unsigned count;std::array<unsigned,bones> map;std::vector<std::int16_t> parents;bool fullRoot=false;};
struct GraphStamp{const RE::BShkbAnimationGraph* graph;const void* skeleton;std::size_t bones;bool operator==(const GraphStamp&)const=default;};
std::mutex mutex;std::vector<GraphBinding> graphs;std::vector<GraphStamp> graphStamp;
const RE::BSAnimationGraphManager* boundManager=nullptr;const RE::NiAVObject* boundRoot=nullptr;
float frameDelta=0;bool airborne=false,filterValid=false;const RE::Node* filteredRoot=nullptr;float filteredHeight=0;
RE::NiPoint3 target,exitTarget,exitPelvis;float progress=-1;bool active=true,entering=false;std::atomic<bool>wrote=false,finished=false;
std::atomic<unsigned>writes=0;
void original(RE::hkbGenerator*,const RE::hkbContext&,const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float){}
${extract('RE::NiMatrix3 Rotation(')}
${extract('void World(')}
${extract('void Sample(')}
${extract('void SampleRider(')}
${extract('bool ResolveTarget(')}
${extract('bool Inverse(')}
${extract('bool WorldBone(')}
${extract('bool LoadRider(')}
${extract('void GeneratePose(')}
${extract('void Motion(')}
${extract('void Update(')}
${extract('void Clear(')}
int main(int argc,char** argv){
 assert(argc==3);std::ifstream in(argv[1],std::ios::binary);std::array<unsigned,4> magic;in.read(reinterpret_cast<char*>(magic.data()),16);
 in.read(reinterpret_cast<char*>(names.data()),sizeof(names));in.read(reinterpret_cast<char*>(parents.data()),sizeof(parents));in.read(reinterpret_cast<char*>(animation.data()),sizeof(animation));assert(in);
 std::array<RE::NiPoint3,bones> exitPositions;std::array<RE::NiMatrix3,bones> exitBasis;World(animation.back().data(),exitPositions,exitBasis);exitPelvis=exitPositions[5];
 assert(LoadRider(argv[2]));
 {std::ifstream file(argv[2],std::ios::binary);std::vector<char> good((std::istreambuf_iterator<char>(file)),{});
  for(float invalid:{-1.f,0.f,std::numeric_limits<float>::quiet_NaN()}){auto bad=good;std::memcpy(bad.data()+20+99*64+99*2+32,&invalid,4);{std::ofstream out("build/seat-invalid-rider.nmr",std::ios::binary);out.write(bad.data(),bad.size());}assert(!LoadRider("build/seat-invalid-rider.nmr"));}
  assert(LoadRider(argv[2]));std::array<Pose,bones> sample;
  for(unsigned frame=0;frame<riderFrames-1;++frame){SampleRider(riderDuration*frame/(riderFrames-1),sample.data());for(unsigned bone=0;bone<bones;++bone)for(unsigned n=0;n<4;++n){assert(std::abs(sample[bone].t[n]-riderAnimation[frame][bone].t[n])<.0001f);assert(std::abs(sample[bone].q[n]-riderAnimation[frame][bone].q[n])<.0001f);}}
 }
 const int headerBytes=16+3*16,bytes=headerBytes+99*sizeof(Pose);std::vector<std::byte> buffer(bytes);
 auto* tracks=reinterpret_cast<RE::hkbGeneratorOutput::Tracks*>(buffer.data());
 tracks->masterHeader={bytes,3,{}};auto& h=tracks->trackHeaders[2];h={99,99,headerBytes,48,1,0,1,0};
 RE::hkbGeneratorOutput output{tracks};auto graph=std::make_shared<RE::BShkbAnimationGraph>();GraphBinding first{graph,99};for(unsigned i=0;i<bones;++i)first.map[i]=i;first.parents.assign(parents.begin(),parents.end());first.fullRoot=true;graphs.push_back(first);RE::hkbContext context{&graph->characterInstance};
 for(float npc:{.5f,1.f,1.24f,2.f,3.f})for(float player:{.65f,1.f,1.5f})for(float phase:{-1.f,0.f,.1f,.3f,.5f,.9f,1.f}){
  std::memcpy(buffer.data()+headerBytes,animation[0].data(),99*sizeof(Pose));
  target={3.75858f*npc/player,-17.65506f*npc/player,53.74204f*npc/player};exitTarget=target;progress=phase;wrote=false;finished=false;
  GeneratePose(nullptr,context,nullptr,output,0);assert(wrote);
  assert(finished==(phase>=1));
  std::array<Pose,bones> result;std::memcpy(result.data(),buffer.data()+headerBytes,sizeof(result));std::array<RE::NiPoint3,bones> ps;std::array<RE::NiMatrix3,bones> rs;
  RE::NiPoint3 expected;assert(ResolveTarget(expected));
  World(result.data(),ps,rs);assert(std::abs(ps[5].x-expected.x)<.001f&&std::abs(ps[5].y-expected.y)<.001f&&std::abs(ps[5].z-expected.z)<.001f);
  if(phase<0)for(unsigned i=0;i<99;i++)if(i!=4)assert(std::memcmp(&result[i],&animation[0][i],sizeof(Pose))==0);
 }
 // Actual Update(), not an assumed target: root lag, different scales/yaws,
 // manual offsets, SaddleBone priority, end-of-dismount world position.
 auto near=[](RE::NiPoint3 a,RE::NiPoint3 b){if(!(std::abs(a.x-b.x)<.003f&&std::abs(a.y-b.y)<.003f&&std::abs(a.z-b.z)<.003f))std::fprintf(stderr,"near failed actual=(%f,%f,%f) expected=(%f,%f,%f)\\n",a.x,a.y,a.z,b.x,b.y,b.z);assert(std::abs(a.x-b.x)<.003f&&std::abs(a.y-b.y)<.003f&&std::abs(a.z-b.z)<.003f);};
 auto resolved=[](){RE::NiPoint3 p;assert(ResolveTarget(p));return p;};
 for(float ns:{.5f,1.f,1.24f,3.f})for(float ps:{.65f,1.f,1.5f})for(float yaw:{0.f,.7f,2.5f})for(bool saddle:{false,true}){
  RE::Node pn,mn,back,wrong;RE::Actor player{&pn,{100,250,-30}},mount{&mn,{110,245,-25}};
  float q[4]{0,0,std::sin(yaw/2),std::cos(yaw/2)},mq[4]{0,0,std::sin((yaw+.4f)/2),std::cos((yaw+.4f)/2)};
  pn.world.scale=ps;pn.world.rotate=Rotation(q);mn.world.scale=ns;mn.world.rotate=Rotation(mq);mn.world.translate={90,220,-40};
  const RE::NiPoint3 backLocal{4,-18,42};back.world.translate=mn.world.translate+mn.world.rotate*backLocal*ns;
  mn.spine=saddle?&wrong:&back;mn.saddle=saddle?&back:nullptr;
  Settings::Data cfg;cfg.seatHeight=12;cfg.seatForward=7;cfg.seatSide=-3;
  // Y4.12 logged: raw bank X can exceed 56 while the displayed back stays near 4.
  // The seat must follow the displayed back, independently of the bank's X.
  Crawl::sample.x=56.325f;
  Update(&player,&mount,cfg,-1,{});assert(active);
  const auto worldSeat=mn.world.translate+mn.world.rotate*RE::NiPoint3{1.f,-11,54}*ns;
  auto expected=pn.world.rotate.Transpose()*(worldSeat-pn.world.translate)/ps;
  near(resolved(),expected);
  near(pn.world.translate+pn.world.rotate*resolved()*ps,worldSeat);
  const auto snapshot=resolved();pn.world.translate={-500,300,50};mn.world.translate=mn.world.translate+RE::NiPoint3{77,22,-15};back.world.translate=back.world.translate+RE::NiPoint3{77,22,-15};
  expected=pn.world.rotate.Transpose()*(mn.world.translate+mn.world.rotate*RE::NiPoint3{1.f,-11,54}*ns-pn.world.translate)/ps;
  near(resolved(),snapshot); // One immutable player-frame snapshot; no cross-thread root reread.
  Update(&player,&mount,cfg,-1,{});near(resolved(),expected);
  const auto rideTarget=resolved();RE::NiPoint3 exit{-72,21,0};
  std::array<RE::NiPoint3,bones>end;std::array<RE::NiMatrix3,bones>basis;World(animation.back().data(),end,basis);
  const auto endTarget=end[5]+pn.world.rotate.Transpose()*(player.position+exit-pn.world.translate)/ps;
  entering=false;Update(&player,&mount,cfg,.5f,exit);near(resolved(),(rideTarget+endTarget)*.5f);
  Update(&player,&mount,cfg,1,exit);near(pn.world.translate+pn.world.rotate*resolved()*ps,player.position+exit+pn.world.rotate*end[5]*ps);
  entering=true;const auto entryTarget=end[5]+pn.world.rotate.Transpose()*(mount.position+exit-pn.world.translate)/ps;
  Update(&player,&mount,cfg,1,exit);near(resolved(),entryTarget);entering=false;
  mn.saddle=mn.spine=nullptr;Update(&player,&mount,cfg,-1,{});assert(!active&&!wrote&&!finished);
  mn.spine=&back;Update(&player,&mount,cfg,-1,{});assert(active);
  pn.world.scale=std::numeric_limits<float>::quiet_NaN();Update(&player,&mount,cfg,-1,{});assert(!active);
 }
 // Grounded local-height smoothing is monotonic; jump and 3D rebuild bypass it.
 for(float fps:{30.f,60.f,144.f}){
  RE::Node pn,mn,back,reloaded;mn.spine=&back;reloaded.spine=&back;RE::Actor p{&pn,{}},m{&mn,{}};Settings::Data cfg;
  filterValid=false;Crawl::sample={4,-18,42};Motion(1/fps,false,0);Update(&p,&m,cfg,-1,{});assert(target.z==54);
  Crawl::sample.z=46;float previous=target.z;
  for(int i=0;i<fps;++i){Update(&p,&m,cfg,-1,{});assert(target.z>=previous&&target.z<=58);previous=target.z;}assert(std::abs(target.z-58)<.001f);
  Crawl::sample.z=42;Motion(1/fps,true,2);Update(&p,&m,cfg,-1,{});assert(target.z==54);
  Crawl::sample.z=46;m.node=&reloaded;Motion(1/fps,false,0);Update(&p,&m,cfg,-1,{});assert(target.z==58);
 }
 frameDelta=0;Crawl::sample={4,-18,42};active=true;
 entering=true;for(float p:{1.f,.5f,0.f}){progress=p;finished=false;GeneratePose(nullptr,context,nullptr,output,0);assert(finished==(p==0));}entering=false;
 // Grounded locomotion uses the exact Phantom Horse clip. Its pelvis X is
 // discarded, while Y/Z motion remains centred and RiderSway-scaled.
 {
  std::memcpy(buffer.data()+headerBytes,animation[0].data(),99*sizeof(Pose));target={7,-11,55};progress=-1;riderSway=1;Motion(.1f,false,3);assert(riderBlend>0&&riderBlend<1);Motion(.02f,false,3);assert(riderBlend==1);wrote=false;
  GeneratePose(nullptr,context,nullptr,output,0);assert(wrote);std::array<RE::NiPoint3,bones> ps;std::array<RE::NiMatrix3,bones> rs,sourceRs;std::array<RE::NiPoint3,bones> sourcePs;
  std::array<Pose,bones> sampled;SampleRider(riderTime,sampled.data());World(sampled.data(),sourcePs,sourceRs);World(reinterpret_cast<Pose*>(buffer.data()+headerBytes),ps,rs);
  near(ps[5],{target.x,target.y+sourcePs[5].y-riderCenter.y,target.z+sourcePs[5].z-riderCenter.z});
  auto* actual=reinterpret_cast<Pose*>(buffer.data()+headerBytes);float orientationDot=0;for(int n=0;n<4;++n){assert(std::abs(actual[6].t[n]-sampled[6].t[n])<1e-5f);orientationDot+=actual[6].q[n]*sampled[6].q[n];assert(std::abs(actual[6].s[n]-sampled[6].s[n])<1e-5f);}assert(std::abs(std::abs(orientationDot)-1.f)<1e-5f);
  Motion(.1f,true,2);assert(riderGait==0&&riderBlend>0);Motion(.1f,true,2);assert(riderBlend==0&&riderTime==0);
 }
 // Poison native root/jump translations: all base pose tracks become rider pose,
 // while the separate extracted-motion track still reaches native physics.
 {
  const int size=bytes+sizeof(Pose);std::vector<std::byte> mixed(size);
  std::memcpy(mixed.data(),buffer.data(),bytes);auto* mt=reinterpret_cast<RE::hkbGeneratorOutput::Tracks*>(mixed.data());mt->masterHeader.numBytes=size;
  mt->trackHeaders[1]={1,1,static_cast<std::int16_t>(bytes),48,1,0,1,0};
  Pose motion=animation[0][0];motion.t[0]=12;motion.t[1]=-7;std::memcpy(mixed.data()+bytes,&motion,sizeof(motion));
  std::memcpy(mixed.data()+headerBytes,animation.back().data(),sizeof(animation[0]));
  auto* native=reinterpret_cast<Pose*>(mixed.data()+headerBytes);
  for(unsigned i=0;i<4;++i){native[i].t[0]+=100;native[i].t[2]+=200;}
  auto cameraExpected=animation.front();
  for(unsigned i=0;i<bones;++i)if(std::string_view(names[i].data()).starts_with("Camera")){
   native[i].t[0]+=73;native[i].t[1]-=41;cameraExpected[i]=native[i];
  }
  RE::hkbGeneratorOutput mo{mt};progress=-1;GeneratePose(nullptr,context,nullptr,mo,0);
  assert(std::memcmp(mixed.data()+bytes,&motion,sizeof(motion))==0);
  auto* actual=reinterpret_cast<Pose*>(mixed.data()+headerBytes);
  for(unsigned i=0;i<bones;++i)if(i!=4){auto& expected=std::string_view(names[i].data()).starts_with("Camera")?cameraExpected[i]:animation.front()[i];assert(std::memcmp(&actual[i],&expected,sizeof(Pose))==0);}
 }
 // Both graph outputs must replace a poisoned native jump/walk pose.
 {
  auto second=std::make_shared<RE::BShkbAnimationGraph>();auto secondBinding=first;secondBinding.graph=second;graphs.push_back(secondBinding);RE::hkbContext secondContext{&second->characterInstance};
  progress=-1;std::memcpy(buffer.data()+headerBytes,animation.back().data(),sizeof(animation[0]));wrote=false;
  GeneratePose(nullptr,secondContext,nullptr,output,0);assert(wrote);
  auto* actual=reinterpret_cast<Pose*>(buffer.data()+headerBytes);
  for(unsigned i=0;i<bones;++i)if(i!=4){auto& expected=std::string_view(names[i].data()).starts_with("Camera")?animation.back()[i]:animation.front()[i];assert(std::memcmp(&actual[i],&expected,sizeof(Pose))==0);}
  graphs.pop_back();
 }
 // A root may move between Tick's Update and native pose generation. Verify
 // final pelvis world position, including airborne translation, from the actual output.
 {
  RE::Node pnode,mnode,back;mnode.spine=&back;pnode.world.scale=.9f;mnode.world.scale=1.9f;
  RE::Actor p{&pnode,{}},m{&mnode,{}};Settings::Data cfg;Crawl::sample={4,-18,42};
  for(int frame=0;frame<90;++frame){
   Motion(1.f/60,true,2);Update(&p,&m,cfg,-1,{});const auto snapshotSeat=resolved();
   pnode.world.translate={float(frame*6),0,float(frame%20)*3};mnode.world.translate=pnode.world.translate+RE::NiPoint3{-11,3,-2};
   GeneratePose(nullptr,context,nullptr,output,0);std::array<RE::NiPoint3,bones> ps;std::array<RE::NiMatrix3,bones> rs;
   World(reinterpret_cast<Pose*>(buffer.data()+headerBytes),ps,rs);
   near(pnode.world.translate+pnode.world.rotate*ps[5]*pnode.world.scale,pnode.world.translate+pnode.world.rotate*snapshotSeat*pnode.world.scale);
  }
 }
 // Dense track shape/size and binding checks must reject before touching memory.
 for(int scenario=0;scenario<8;scenario++){
  tracks->masterHeader={bytes,3,{}};h={99,99,headerBytes,48,1,0,1,0};
  if(scenario==0)h.numData=98;if(scenario==1)h.capacity=98;if(scenario==2)h.flags=1;if(scenario==3)h.flags=4;
  if(scenario==4)h.dataOffset=headerBytes-1;if(scenario==5)tracks->masterHeader.numBytes=bytes-1;if(scenario==6)h.type=0;if(scenario==7)h.elementSizeBytes=32;
  wrote=false;auto before=buffer;GeneratePose(nullptr,context,nullptr,output,0);assert(!wrote&&buffer==before);
 }
 RE::NiMatrix3 matrix;matrix.entry[0][0]=2;matrix.entry[0][1]=.3f;matrix.entry[1][0]=.7f;matrix.entry[1][1]=3;matrix.entry[2][2]=.5f;
 RE::NiMatrix3 inverse;assert(Inverse(matrix,inverse));auto identity=matrix*inverse;for(int i=0;i<3;i++)for(int j=0;j<3;j++)assert(std::abs(identity.entry[i][j]-(i==j))<1e-5);
 // Mapped bone beyond the shortened output must be rejected before any write.
 tracks->masterHeader={bytes,3,{}};h={99,99,headerBytes,48,1,0,1,0};graphs[0].count=126;graphs[0].parents.resize(126,0);graphs[0].map[6]=100;
 {auto before=buffer;wrote=false;GeneratePose(nullptr,context,nullptr,output,0);assert(!wrote&&before==buffer);}graphs[0].map[6]=6;
 // Parent-chain calculation agrees with the full-skeleton reference.
 for(const auto& frame:riderAnimation){std::vector<Pose> poses(frame.begin(),frame.end());std::vector<std::int16_t> hierarchy(parents.begin(),parents.end());std::array<RE::NiPoint3,bones> points;std::array<RE::NiMatrix3,bones> bases;World(frame.data(),points,bases);
  for(int bone:{3,4,5,12}){RE::NiPoint3 point;RE::NiMatrix3 basis;assert(WorldBone(poses,hierarchy,bone,point,basis));near(point,points[bone]);for(int i=0;i<3;++i)for(int j=0;j<3;++j)assert(std::abs(basis.entry[i][j]-bases[bone].entry[i][j])<.0001f);}}
 // XPMSSE 126-track output: extension tracks are preserved in every phase.
 const int extendedBytes=headerBytes+126*sizeof(Pose);std::vector<std::byte> extended(extendedBytes,std::byte{0x5a});
 auto* xt=reinterpret_cast<RE::hkbGeneratorOutput::Tracks*>(extended.data());xt->masterHeader={extendedBytes,3,{}};xt->trackHeaders[2]={126,126,headerBytes,48,1,0,1,0};
 RE::hkbGeneratorOutput xo{xt};graphs[0].count=126;graphs[0].parents.resize(126,0);
 for(float phase:{-1.f,0.f,.5f,1.f}){std::memcpy(extended.data()+headerBytes,animation[0].data(),sizeof(animation[0]));progress=phase;wrote=false;GeneratePose(nullptr,context,nullptr,xo,0);assert(wrote);
  for(unsigned i=headerBytes+99*sizeof(Pose);i<extended.size();++i)assert(extended[i]==std::byte{0x5a});}
 graphs[0].count=99;graphs[0].parents.resize(99);wrote=false;auto unchanged=extended;GeneratePose(nullptr,context,nullptr,xo,0);assert(!wrote&&extended==unchanged);
 Clear();assert(!active&&!wrote&&!finished&&graphs.empty());
 std::puts("PASS: static/Phantom rider sources, shared-back X and zero extra lateral rider motion, mount-relative entry, native camera/extracted motion, pelvis targeting/scales/output bounds. Engine rendering untested.");
}
`);
