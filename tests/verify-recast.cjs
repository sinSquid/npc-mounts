const fs=require('fs'),path=require('path'),assert=require('assert');
const source=fs.readFileSync(path.join(__dirname,'../src/plugin.cpp'),'utf8');
const start=source.indexOf('    if (Converted(actor)) {',source.indexOf('bool Convert('));assert(start>0);
let end=source.indexOf('{',start)+1,depth=1;while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}
fs.writeFileSync(path.join(__dirname,'../build/recast-regression.cpp'),`
#include <cassert>
#include <cstdio>
#include <unordered_map>
namespace RE{struct Actor{unsigned id=1;bool ready=true,dead=false,riding=false,bound=false,busy=false;int resets=0,holds=0,stats=0;unsigned GetFormID(){return id;}bool IsDead(){return dead;}};}
struct State{bool restoreRequested=false;float requestedScale=1,originalScale=.8f,speedDelta=60;unsigned originalFlags=123;};
std::unordered_map<unsigned,State>targets{{1,{}}};
bool Converted(RE::Actor* a){return a&&targets.contains(a->id);}bool Restoring(RE::Actor* a){return targets.at(a->id).restoreRequested;}bool ActorReady(RE::Actor* a){return a->ready;}bool Riding(RE::Actor* a){return a->riding;}
namespace Extras{bool Bound(RE::Actor* a){return a->bound;}bool Busy(RE::Actor* a){return a->busy;}}
bool ConversionRejected(RE::Actor*,const char*){return false;}
void HoldPose(RE::Actor* a){++a->holds;}void ResetPose(RE::Actor* a,State&){++a->resets;}void ApplyStats(RE::Actor* a,State& s){assert(s.requestedScale==-1);++a->stats;}
bool Recast(RE::Actor* actor){${source.slice(start,end)}return false;}
int main(){RE::Actor actor;for(int i=0;i<1000;i++)assert(Recast(&actor));assert(actor.resets==1000&&actor.stats==1000&&actor.holds==1000);
 auto& s=targets.at(1);assert(s.originalScale==.8f&&s.speedDelta==60&&s.originalFlags==123);
 actor.dead=true;assert(!Recast(&actor));actor.dead=false;actor.ready=false;assert(!Recast(&actor));actor.ready=true;
 s.restoreRequested=true;assert(!Recast(&actor));s.restoreRequested=false;
 for(bool* flag:{&actor.riding,&actor.bound,&actor.busy}){*flag=true;assert(Recast(&actor)&&actor.resets==1000);*flag=false;}
 puts("PASS: actual recast branch repairs lost pose/faction 1000 times without replacing original restoration ledger; pending restore, death, unload and owned restraint boundaries.");}
`);
