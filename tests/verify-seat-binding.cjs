const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'src/Seat.cpp'),'utf8');
function take(sig){const start=source.indexOf(sig);assert(start>=0);let end=source.indexOf('{',start)+1,n=1;while(n){const c=source[end++];if(c==='{')n++;if(c==='}')n--;}return source.slice(start,end);}
fs.writeFileSync(path.join(root,'build/seat-binding-regression.cpp'),`
#include <vector>
#include <array>
#include <string>
#include <string_view>
#include <memory>
#include <mutex>
#include <cstring>
#include <cctype>
#include <limits>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <atomic>
namespace spdlog {template<class...T>void warn(T...){ }template<class...T>void info(T...){ }}
namespace RE {
template<class T>using BSTSmartPointer=std::shared_ptr<T>;
struct Skeleton{struct Bone{std::string name;};std::vector<Bone>bones;std::vector<short>parentIndices;};
struct SkeletonPtr{Skeleton* p{};SkeletonPtr()=default;SkeletonPtr(Skeleton* value):p(value){}Skeleton* get()const{return p;}Skeleton* operator->()const{return p;}explicit operator bool()const{return p;}};
struct Setup{SkeletonPtr animationSkeleton;};struct Character{Setup* setup;};
struct BShkbAnimationGraph{Character characterInstance;};
struct BSAnimationGraphManager{std::vector<BSTSmartPointer<BShkbAnimationGraph>>graphs;};
struct NiAVObject{};struct Actor{BSTSmartPointer<BSAnimationGraphManager> manager;NiAVObject root;bool GetAnimationGraphManager(BSTSmartPointer<BSAnimationGraphManager>& m){m=manager;return bool(m);}NiAVObject* Get3D(){return &root;}};
}
constexpr unsigned bones=99;std::array<std::array<char,64>,bones>names;std::array<short,bones>parents;
struct GraphBinding{RE::BSTSmartPointer<RE::BShkbAnimationGraph>graph;unsigned count;std::array<unsigned,bones>map;std::vector<std::int16_t>parents;bool fullRoot=false;};
struct GraphStamp{const RE::BShkbAnimationGraph*graph;const void*skeleton;std::size_t bones;bool operator==(const GraphStamp&)const=default;};std::vector<GraphBinding>graphs;std::vector<GraphStamp>graphStamp;std::mutex mutex;
const RE::BSAnimationGraphManager* boundManager=nullptr;const RE::NiAVObject* boundRoot=nullptr;
bool loaded=true,active=true;std::atomic<bool>wrote=true,finished=true;const char* bindError="";void Clear(){graphs.clear();graphStamp.clear();boundManager=nullptr;boundRoot=nullptr;active=false;wrote=false;finished=false;}
${take('std::string_view Canonical(')}
${take('std::string BoneKey(')}
${take('std::vector<GraphStamp> Snapshot(')}
${take('bool Matches(')}
${take('std::vector<GraphBinding> Collect(')}
${take('bool Bind(')}
${take('void DropBindings(')}
${take('bool Refresh(')}
int main(int argc,char**argv){assert(argc==2);std::ifstream f(argv[1],std::ios::binary);f.seekg(16);f.read(reinterpret_cast<char*>(names.data()),sizeof(names));f.read(reinterpret_cast<char*>(parents.data()),sizeof(parents));assert(f);
 RE::Skeleton skeleton;for(unsigned i=0;i<bones;++i){skeleton.bones.push_back({names[i].data()});skeleton.parentIndices.push_back(parents[i]);}
 RE::Setup setup{&skeleton};auto graph=std::make_shared<RE::BShkbAnimationGraph>();graph->characterInstance.setup=&setup;
 RE::Actor actor{std::make_shared<RE::BSAnimationGraphManager>()};actor.manager->graphs.push_back(graph);
 assert(Bind(&actor)&&graphs.size()==1&&graphs[0].count==99);
 wrote=true;assert(Refresh(&actor)&&graphs.size()==1&&wrote);
 auto waterGraph=std::make_shared<RE::BShkbAnimationGraph>();waterGraph->characterInstance.setup=&setup;actor.manager->graphs.push_back(waterGraph);
 assert(Refresh(&actor)&&graphs.size()==2&&!wrote);actor.manager->graphs.erase(actor.manager->graphs.begin());assert(Refresh(&actor)&&graphs.size()==1);
 graph=waterGraph;
 for(unsigned i=99;i<126;++i){skeleton.bones.push_back({"XPMSSE extension"});skeleton.parentIndices.push_back(5);}assert(Bind(&actor)&&graphs[0].count==126);
 auto alias=skeleton;alias.bones[1].name="NPC LookNode [Look]";
 RE::Setup aliasSetup{&alias};auto aliasGraph=std::make_shared<RE::BShkbAnimationGraph>();aliasGraph->characterInstance.setup=&aliasSetup;
 actor.manager->graphs.push_back(aliasGraph);assert(Bind(&actor)&&graphs.size()==2);actor.manager->graphs.pop_back();
 skeleton.bones[5].name="wrong";assert(!Bind(&actor)&&graphs.empty());skeleton.bones[5].name=names[5].data();
 assert(Bind(&actor));auto broken=skeleton;broken.bones[5].name="wrong";RE::Setup brokenSetup{&broken};auto brokenGraph=std::make_shared<RE::BShkbAnimationGraph>();brokenGraph->characterInstance.setup=&brokenSetup;
 actor.manager->graphs={brokenGraph};active=wrote=finished=true;assert(!Refresh(&actor)&&graphs.empty()&&!active&&!wrote&&!finished);actor.manager->graphs={waterGraph};
 skeleton.parentIndices[5]=0;assert(!Bind(&actor));skeleton.parentIndices[5]=parents[5];
 skeleton.parentIndices.pop_back();assert(!Bind(&actor));skeleton.bones.resize(98);skeleton.parentIndices.resize(98);assert(!Bind(&actor));
 actor.manager.reset();assert(!Bind(&actor));loaded=false;assert(!Bind(&actor));
 std::puts("PASS: actual Bind accepts 99/126 compatible skeletons; rejects name/parent/size mismatch and missing resources; failed rebinding clears stale graphs.");
}
`);
