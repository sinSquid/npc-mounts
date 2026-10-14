// Actual pose math/output function with dense-buffer test doubles and real samples.
const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/Seat.cpp'),'utf8');
function extract(sig){const start=source.indexOf(sig);assert(start>=0);let end=source.indexOf('{',start)+1,depth=1;while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}return source.slice(start,end);}
fs.writeFileSync(path.join(repo,'build/seat-regression.cpp'),`
#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>
#include <cstring>
#include <fstream>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include "../src/Config.h"
namespace RE {
struct NiPoint3{float x{},y{},z{};NiPoint3 operator+(NiPoint3 b)const{return{x+b.x,y+b.y,z+b.z};}NiPoint3 operator-(NiPoint3 b)const{return{x-b.x,y-b.y,z-b.z};}NiPoint3 operator*(float k)const{return{x*k,y*k,z*k};}NiPoint3 operator/(float k)const{return{x/k,y/k,z/k};}};
struct NiMatrix3 {float entry[3][3]{{1,0,0},{0,1,0},{0,0,1}};
 NiMatrix3 Transpose()const{NiMatrix3 r;for(int i=0;i<3;i++)for(int j=0;j<3;j++)r.entry[i][j]=entry[j][i];return r;}
 NiPoint3 operator*(NiPoint3 p)const{return{entry[0][0]*p.x+entry[0][1]*p.y+entry[0][2]*p.z,entry[1][0]*p.x+entry[1][1]*p.y+entry[1][2]*p.z,entry[2][0]*p.x+entry[2][1]*p.y+entry[2][2]*p.z};}
 NiMatrix3 operator*(const NiMatrix3& b)const{NiMatrix3 r;for(int i=0;i<3;i++)for(int j=0;j<3;j++){r.entry[i][j]=0;for(int k=0;k<3;k++)r.entry[i][j]+=entry[i][k]*b.entry[k][j];}return r;}
};
struct Node {struct {NiPoint3 translate;NiMatrix3 rotate;float scale=1;}world;Node* saddle=nullptr;Node* spine=nullptr;
 Node* GetObjectByName(const char* name){return std::strcmp(name,"SaddleBone")==0?saddle:spine;}};
struct Actor{Node* node;NiPoint3 position;Node* Get3D(){return node;}NiPoint3 GetPosition(){return position;}};
struct hkbGenerator{};struct hkbCharacter{};struct hkbContext{hkbCharacter* character;};
struct BShkbAnimationGraph{hkbCharacter characterInstance;};
struct hkbGeneratorOutput{
 struct Master{int numBytes,numTracks;std::uint8_t unused[8];};
 struct Header{std::int16_t capacity,numData,dataOffset,elementSizeBytes;float onFraction;std::int8_t flags,type;std::uint16_t pad;};
 struct Tracks{Master masterHeader;Header trackHeaders[1];};Tracks* tracks;
};
}
constexpr unsigned bones=99,frames=31;
struct Pose{float t[4],q[4],s[4];};
std::array<std::array<Pose,bones>,frames> animation;
std::array<std::array<char,64>,bones> names;std::array<std::int16_t,bones> parents;
struct GraphBinding{std::shared_ptr<RE::BShkbAnimationGraph> graph;unsigned count;};
std::mutex mutex;std::vector<GraphBinding> graphs;
RE::NiPoint3 target;float progress=-1;bool active=true;std::atomic<bool>wrote=false,finished=false;
void original(RE::hkbGenerator*,const RE::hkbContext&,const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float){}
${extract('RE::NiMatrix3 Rotation(')}
${extract('void World(')}
${extract('void Sample(')}
${extract('bool Inverse(')}
${extract('void GeneratePose(')}
${extract('void Update(')}
${extract('void Clear(')}
int main(int argc,char** argv){
 assert(argc==2);std::ifstream in(argv[1],std::ios::binary);std::array<unsigned,4> magic;in.read(reinterpret_cast<char*>(magic.data()),16);
 in.read(reinterpret_cast<char*>(names.data()),sizeof(names));in.read(reinterpret_cast<char*>(parents.data()),sizeof(parents));in.read(reinterpret_cast<char*>(animation.data()),sizeof(animation));assert(in);
 const int headerBytes=16+3*16,bytes=headerBytes+99*sizeof(Pose);std::vector<std::byte> buffer(bytes);
 auto* tracks=reinterpret_cast<RE::hkbGeneratorOutput::Tracks*>(buffer.data());
 tracks->masterHeader={bytes,3,{}};auto& h=tracks->trackHeaders[2];h={99,99,headerBytes,48,1,0,1,0};
 RE::hkbGeneratorOutput output{tracks};auto graph=std::make_shared<RE::BShkbAnimationGraph>();graphs.push_back({graph,99});RE::hkbContext context{&graph->characterInstance};
 for(float npc:{.5f,1.f,1.24f,2.f,3.f})for(float player:{.65f,1.f,1.5f})for(float phase:{-1.f,0.f,.1f,.3f,.5f,.9f,1.f}){
  std::memcpy(buffer.data()+headerBytes,animation[0].data(),99*sizeof(Pose));
  target={3.75858f*npc/player,-17.65506f*npc/player,53.74204f*npc/player};progress=phase;wrote=false;finished=false;
  GeneratePose(nullptr,context,nullptr,output,0);assert(wrote);
  assert(finished==(phase>=1));
  std::array<Pose,bones> result;std::memcpy(result.data(),buffer.data()+headerBytes,sizeof(result));std::array<RE::NiPoint3,bones> ps;std::array<RE::NiMatrix3,bones> rs;
  World(result.data(),ps,rs);assert(std::abs(ps[5].x-target.x)<.001f&&std::abs(ps[5].y-target.y)<.001f&&std::abs(ps[5].z-target.z)<.001f);
  if(phase<0)for(unsigned i=0;i<99;i++)if(i!=4)assert(std::memcmp(&result[i],&animation[0][i],sizeof(Pose))==0);
 }
 // Actual Update(), not an assumed target: root lag, different scales/yaws,
 // manual offsets, SaddleBone priority, end-of-dismount world position.
 auto near=[](RE::NiPoint3 a,RE::NiPoint3 b){assert(std::abs(a.x-b.x)<.003f&&std::abs(a.y-b.y)<.003f&&std::abs(a.z-b.z)<.003f);};
 for(float ns:{.5f,1.f,1.24f,3.f})for(float ps:{.65f,1.f,1.5f})for(float yaw:{0.f,.7f,2.5f})for(bool saddle:{false,true}){
  RE::Node pn,mn,back,wrong;RE::Actor player{&pn,{100,250,-30}},mount{&mn,{110,245,-25}};
  float q[4]{0,0,std::sin(yaw/2),std::cos(yaw/2)},mq[4]{0,0,std::sin((yaw+.4f)/2),std::cos((yaw+.4f)/2)};
  pn.world.scale=ps;pn.world.rotate=Rotation(q);mn.world.scale=ns;mn.world.rotate=Rotation(mq);mn.world.translate={90,220,-40};
  const RE::NiPoint3 backLocal{4,-18,42};back.world.translate=mn.world.translate+mn.world.rotate*backLocal*ns;
  mn.spine=saddle?&wrong:&back;mn.saddle=saddle?&back:nullptr;
  Settings::Data cfg;cfg.seatHeight=12;cfg.seatForward=7;cfg.seatSide=-3;
  Update(&player,&mount,cfg,-1,{});assert(active);
  const auto expected=mount.position+mn.world.rotate*(backLocal+RE::NiPoint3{-3,7,12})*ns;
  near(player.position+pn.world.rotate*target*ps,expected);
  const auto rideTarget=target;RE::NiPoint3 exit{-72,21,0};
  std::array<RE::NiPoint3,bones>end;std::array<RE::NiMatrix3,bones>basis;World(animation.back().data(),end,basis);
  const auto endTarget=end[5]+pn.world.rotate.Transpose()*exit/ps;
  Update(&player,&mount,cfg,.5f,exit);near(target,(rideTarget+endTarget)*.5f);
  Update(&player,&mount,cfg,1,exit);near(player.position+pn.world.rotate*target*ps,player.position+exit+pn.world.rotate*end[5]*ps);
  mn.saddle=mn.spine=nullptr;Update(&player,&mount,cfg,-1,{});assert(!active&&!wrote&&!finished);
  mn.spine=&back;Update(&player,&mount,cfg,-1,{});assert(active);
  pn.world.scale=std::numeric_limits<float>::quiet_NaN();Update(&player,&mount,cfg,-1,{});assert(!active);
 }
 active=true;
 // Dense track shape/size and binding checks must reject before touching memory.
 for(int scenario=0;scenario<8;scenario++){
  tracks->masterHeader={bytes,3,{}};h={99,99,headerBytes,48,1,0,1,0};
  if(scenario==0)h.numData=98;if(scenario==1)h.capacity=98;if(scenario==2)h.flags=1;if(scenario==3)h.flags=4;
  if(scenario==4)h.dataOffset=headerBytes-1;if(scenario==5)tracks->masterHeader.numBytes=bytes-1;if(scenario==6)h.type=0;if(scenario==7)h.elementSizeBytes=32;
  wrote=false;auto before=buffer;GeneratePose(nullptr,context,nullptr,output,0);assert(!wrote&&buffer==before);
 }
 RE::NiMatrix3 matrix;matrix.entry[0][0]=2;matrix.entry[0][1]=.3f;matrix.entry[1][0]=.7f;matrix.entry[1][1]=3;matrix.entry[2][2]=.5f;
 RE::NiMatrix3 inverse;assert(Inverse(matrix,inverse));auto identity=matrix*inverse;for(int i=0;i<3;i++)for(int j=0;j<3;j++)assert(std::abs(identity.entry[i][j]-(i==j))<1e-5);
 // XPMSSE 126-track output: extension tracks are preserved in every phase.
 const int extendedBytes=headerBytes+126*sizeof(Pose);std::vector<std::byte> extended(extendedBytes,std::byte{0x5a});
 auto* xt=reinterpret_cast<RE::hkbGeneratorOutput::Tracks*>(extended.data());xt->masterHeader={extendedBytes,3,{}};xt->trackHeaders[2]={126,126,headerBytes,48,1,0,1,0};
 RE::hkbGeneratorOutput xo{xt};graphs[0].count=126;
 for(float phase:{-1.f,0.f,.5f,1.f}){std::memcpy(extended.data()+headerBytes,animation[0].data(),sizeof(animation[0]));progress=phase;wrote=false;GeneratePose(nullptr,context,nullptr,xo,0);assert(wrote);
  for(unsigned i=headerBytes+99*sizeof(Pose);i<extended.size();++i)assert(extended[i]==std::byte{0x5a});}
 graphs[0].count=99;wrote=false;auto unchanged=extended;GeneratePose(nullptr,context,nullptr,xo,0);assert(!wrote&&extended==unchanged);
 Clear();assert(!active&&!wrote&&!finished&&graphs.empty());
 std::puts("PASS: actual seat output math over scale 0.5-3 and player 0.65-1.5; 7 animation phases; only COM changed while riding; malformed/sparse/additive tracks rejected. Engine rendering untested.");
}
`);
