// Exercise the actual transition helper; event observers must see the new rank.
const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(repo,'src/plugin.cpp'),'utf8');
const start=source.indexOf('void SetMotion(');assert(start>=0);
let end=source.indexOf('{',start)+1,depth=1;
while(depth){const c=source[end++];if(c==='{')++depth;if(c==='}')--depth;}
const code=`
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
struct State {int animation=0;};
struct {int logLevel=4;}config;
void* mountFaction{};
namespace RE {
struct Actor {
 unsigned GetFormID(){return 1;}
 int rank=0,expected=0,writes=0;std::vector<std::string> events;
 void AddToFaction(void*,std::int8_t value){rank=value;++writes;}
 void NotifyAnimationGraph(const char* event){assert(rank==expected);events.emplace_back(event);}
};
}
${source.slice(start,end)}
int main(){
 RE::Actor actor;State state;
 actor.expected=1;SetMotion(&actor,state,1);assert(actor.events==std::vector<std::string>{"moveStart"});
 actor.events.clear();actor.expected=3;SetMotion(&actor,state,3);assert(actor.events==std::vector<std::string>{"SprintStart"});
 actor.events.clear();actor.expected=7;SetMotion(&actor,state,7);
 assert((actor.events==std::vector<std::string>{"SprintStop","moveStop"}));
 actor.events.clear();actor.expected=1;SetMotion(&actor,state,1);assert(actor.events==std::vector<std::string>{"moveStart"});
 actor.events.clear();auto writes=actor.writes;SetMotion(&actor,state,1);assert(actor.events.empty()&&actor.writes==writes);
 actor.expected=0;SetMotion(&actor,state,0);assert(actor.events==std::vector<std::string>{"moveStop"});
 std::puts("PASS: actual transition publishes rank before events; sprint/jump/landing/idle and unchanged state. Not engine QA.");
}
`;
fs.writeFileSync(path.join(repo,'build/transition-regression.cpp'),code);
