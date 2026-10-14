const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'src/Seat.cpp'),'utf8');
function take(sig){const start=source.indexOf(sig);assert(start>=0);let end=source.indexOf('{',start)+1,n=1;while(n){const c=source[end++];if(c==='{')n++;if(c==='}')n--;}return source.slice(start,end);}
fs.writeFileSync(path.join(root,'build/seat-binding-regression.cpp'),`
#include <vector>
#include <array>
#include <string>
#include <memory>
#include <mutex>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <fstream>
namespace spdlog {template<class...T>void warn(T...){ }template<class...T>void info(T...){ }}
namespace RE {
template<class T>using BSTSmartPointer=std::shared_ptr<T>;
struct Skeleton{struct Bone{std::string name;};std::vector<Bone>bones;std::vector<short>parentIndices;};
struct Setup{Skeleton* animationSkeleton;};struct Character{Setup* setup;};
struct BShkbAnimationGraph{Character characterInstance;};
struct BSAnimationGraphManager{std::vector<BSTSmartPointer<BShkbAnimationGraph>>graphs;};
struct Actor{BSTSmartPointer<BSAnimationGraphManager> manager;bool GetAnimationGraphManager(BSTSmartPointer<BSAnimationGraphManager>& m){m=manager;return bool(m);}};
}
constexpr unsigned bones=99;std::array<std::array<char,64>,bones>names;std::array<short,bones>parents;
struct GraphBinding{RE::BSTSmartPointer<RE::BShkbAnimationGraph>graph;unsigned count;};std::vector<GraphBinding>graphs;std::mutex mutex;
bool loaded=true;const char* bindError="";void Clear(){graphs.clear();}
${take('bool Bind(')}
int main(int argc,char**argv){assert(argc==2);std::ifstream f(argv[1],std::ios::binary);f.seekg(16);f.read(reinterpret_cast<char*>(names.data()),sizeof(names));f.read(reinterpret_cast<char*>(parents.data()),sizeof(parents));assert(f);
 RE::Skeleton skeleton;for(unsigned i=0;i<bones;++i){skeleton.bones.push_back({names[i].data()});skeleton.parentIndices.push_back(parents[i]);}
 RE::Setup setup{&skeleton};auto graph=std::make_shared<RE::BShkbAnimationGraph>();graph->characterInstance.setup=&setup;
 RE::Actor actor{std::make_shared<RE::BSAnimationGraphManager>()};actor.manager->graphs.push_back(graph);
 assert(Bind(&actor)&&graphs.size()==1&&graphs[0].count==99);
 for(unsigned i=99;i<126;++i){skeleton.bones.push_back({"XPMSSE extension"});skeleton.parentIndices.push_back(5);}assert(Bind(&actor)&&graphs[0].count==126);
 skeleton.bones[5].name="wrong";assert(!Bind(&actor)&&graphs.empty());skeleton.bones[5].name=names[5].data();
 skeleton.parentIndices[5]=0;assert(!Bind(&actor));skeleton.parentIndices[5]=parents[5];
 skeleton.parentIndices.pop_back();assert(!Bind(&actor));skeleton.bones.resize(98);skeleton.parentIndices.resize(98);assert(!Bind(&actor));
 actor.manager.reset();assert(!Bind(&actor));loaded=false;assert(!Bind(&actor));
 std::puts("PASS: actual Bind accepts 99/126 compatible skeletons; rejects name/parent/size mismatch and missing resources; failed rebinding clears stale graphs.");
}
`);
