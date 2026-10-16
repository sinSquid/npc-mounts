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
namespace RE{
using FormID=unsigned;template<class T>using BSTSmartPointer=std::shared_ptr<T>;struct NiAVObject{};
struct Bone{std::string name;};struct Skeleton{std::vector<Bone>bones;std::vector<int>parentIndices;};struct Setup{std::shared_ptr<Skeleton>animationSkeleton;};struct Character{std::shared_ptr<Setup>setup;};struct BShkbAnimationGraph{Character characterInstance;};struct BSAnimationGraphManager{std::vector<std::shared_ptr<BShkbAnimationGraph>>graphs;};
struct Actor{unsigned id=1;NiAVObject root;std::shared_ptr<BSAnimationGraphManager>manager=std::make_shared<BSAnimationGraphManager>();unsigned GetFormID(){return id;}NiAVObject* Get3D(){return &root;}bool GetAnimationGraphManager(std::shared_ptr<BSAnimationGraphManager>& out){out=manager;return true;}};
struct hkbGenerator{};struct hkbContext{Character* character;};struct hkbGeneratorOutput{struct Master{int numBytes,numTracks;std::uint8_t unused[8];};struct Header{std::int16_t capacity,numData,dataOffset,elementSizeBytes;float onFraction;std::int8_t flags,type;std::uint16_t pad;};struct Tracks{Master masterHeader;Header trackHeaders[1];};Tracks* tracks;};}
namespace spdlog{template<class...T>void warn(const char*,T...){}template<class...T>void info(const char*,T...){}}
${globals}
void original(RE::hkbGenerator*,const RE::hkbContext&,const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float){}
${['void Blend(','void Sample(','std::string_view Canonical(','void GeneratePose(','bool Bind(','void Update(','void Remove(','void Clear('].map(take).join('\n')}
int main(int argc,char** argv){assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::array<unsigned,4> head;f.read((char*)head.data(),16);assert((head==std::array<unsigned,4>{0x434d504e,1,99,7}));f.read((char*)names.data(),sizeof(names));f.read((char*)parents.data(),sizeof(parents));for(auto& c:clips){unsigned count;f.read((char*)&c.duration,4);f.read((char*)&count,4);c.frames.resize(count);f.read((char*)c.frames.data(),count*sizeof(Frame));}assert(f&&f.peek()==EOF);loaded=true;
 for(auto& c:clips){Frame a,b;Sample(c,c.duration*.2f,a);Sample(c,c.duration*.7f,b);assert(memcmp(&a,&b,sizeof(Frame))!=0);for(const auto& p:a){float norm=0;for(float v:p.q){assert(std::isfinite(v));norm+=v*v;}assert(std::abs(norm-1)<.0001f);}}
 for(unsigned count:{99u,126u}){
 RE::Actor a;auto g=std::make_shared<RE::BShkbAnimationGraph>();g->characterInstance.setup=std::make_shared<RE::Setup>();auto sk=std::make_shared<RE::Skeleton>();g->characterInstance.setup->animationSkeleton=sk;a.manager->graphs.push_back(g);
 for(unsigned i=0;i<count;i++){sk->bones.push_back({i<99?names[i].data():"Custom"+std::to_string(i)});sk->parentIndices.push_back(i<99?parents[i]:0);}
 for(auto& bone:sk->bones)if(bone.name=="NPC LookNode [Look]")bone.name="x_NPC LookNode [Look]";
 assert(Bind(&a));for(float fps:{30.f,60.f,144.f})for(int gait=0;gait<7;gait++){for(int i=0;i<int(fps*2);i++)Update(&a,gait,1/fps);for(auto& p:actors.at(1).pose){float norm=0;for(float v:p.q){assert(std::isfinite(v));norm+=v*v;}assert(std::abs(norm-1)<.0001f);}}
 for(int old:{1,2,3})for(int next:{1,2,3}){auto& st=actors.at(1);st.gait=old;st.time=clips[old].duration*.37f;Update(&a,next,0);assert(std::abs(st.time/clips[next].duration-.37f)<.00001f);}
 Update(&a,0,0);assert(actors.at(1).time==0);Update(&a,1,0);assert(actors.at(1).time==0);Update(&a,4,0);assert(actors.at(1).time==0);
 const unsigned header=64,bytes=header+count*sizeof(Pose);std::vector<std::byte>b(bytes,std::byte{0x33});auto* t=(RE::hkbGeneratorOutput::Tracks*)b.data();t->masterHeader={int(bytes),3,{}};auto& h=t->trackHeaders[2];h={short(count),short(count),64,48,1,0,1,0};RE::hkbGeneratorOutput out{t};RE::hkbContext ctx{&g->characterInstance};auto before=b;GeneratePose(nullptr,ctx,nullptr,out,0);assert(memcmp(b.data()+64,actors.at(1).pose.data(),sizeof(Frame))==0);assert(memcmp(b.data()+64+sizeof(Frame),before.data()+64+sizeof(Frame),(count-99)*sizeof(Pose))==0);
 for(int bad=0;bad<5;bad++){h={short(count),short(count),64,48,1,0,1,0};if(bad==0)h.numData=98;if(bad==1)h.flags=1;if(bad==2)h.elementSizeBytes=32;if(bad==3)h.dataOffset=63;if(bad==4)h.type=0;before=b;Update(&a,2,.1f);GeneratePose(nullptr,ctx,nullptr,out,0);assert(b==before);}
 auto name=sk->bones[5].name;sk->bones[5].name="unsupported";assert(!Bind(&a)&&actors.empty());for(int i=0;i<10;i++)Update(&a,0,.01f);assert(actors.empty());sk->bones[5].name=name;assert(Bind(&a));int parent=sk->parentIndices[5];sk->parentIndices[5]=98;assert(!Bind(&a));sk->parentIndices[5]=parent;assert(Bind(&a));Remove(1);assert(actors.empty()&&retries.empty());}
 Clear();assert(actors.empty()&&retries.empty());std::puts("PASS: real 7-clip human crawl bank; production 99/126-bone binding, helper alias, parent/name rejection, 30/60/144 FPS sampling, output bounds, extra bones preserved and retry/removal.");}
`);
