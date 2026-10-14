const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/Config.cpp'),'utf8');
function extract(sig){const start=source.indexOf(sig);assert(start>=0);let end=source.indexOf('{',start)+1,depth=1;while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}return source.slice(start,end);}
fs.writeFileSync(path.join(repo,'build/config-regression.cpp'),`
#include "../src/Config.h"
#include "../src/RideControl.h"
#include <cassert>
#include <charconv>
#include <fstream>
#include <limits>
#include <iomanip>
#include <cstdio>
namespace Settings {${extract('Data Read(')} ${extract('bool Write(')}}
int main(){
 HoldGate hold;hold.Press();assert(!hold.Tick(.25f,.25f));assert(hold.Tick(.001f,.25f));assert(!hold.Tick(1,.25f));
 hold.Press();assert(!hold.Tick(.2f,.25f));hold.Release();assert(!hold.Tick(1,.25f));
 for(int fps:{30,60,144}){hold.Press();int ticks=0;while(!hold.Tick(1.f/fps,.25f))++ticks;assert((ticks+1.f)/fps>=.25f);}
 Settings::Data cfg;cfg.npcScale=9;cfg.speedMultiplier=.2f;cfg.holdSeconds=.1f;cfg.runThreshold=600;cfg.sprintThreshold=100;
 cfg.seatHeight=std::numeric_limits<float>::quiet_NaN();Settings::Normalize(cfg);
 assert(cfg.npcScale==3&&cfg.speedMultiplier==1&&cfg.holdSeconds==.25f&&cfg.sprintThreshold==620&&cfg.seatHeight==12);
 cfg.overrideScale=true;cfg.npcScale=.51234567f;cfg.speedMultiplier=1.2345678f;cfg.dismountKey=18;
 assert(Settings::Write("build/config-roundtrip.ini",cfg));auto loaded=Settings::Read("build/config-roundtrip.ini");
 for(auto f:Settings::floats)assert(cfg.*f.member==loaded.*f.member);
 for(auto f:Settings::bools)assert(cfg.*f.member==loaded.*f.member);
 assert(loaded.dismountKey==18);
 bool success=true;Settings::Read("build/no-such-config-file.ini",&success);assert(!success);
 {std::ofstream f("build/config-bom.ini",std::ios::binary);f<<"\\xEF\\xBB\\xBF"<<"NPCScale=2.5\\nSpeedMultiplier=1.5\\n";}
 loaded=Settings::Read("build/config-bom.ini",&success);assert(success&&loaded.npcScale==2.5f&&loaded.speedMultiplier==1.5f);
 {std::ofstream f("build/config-invalid.ini");f<<"[Mounts]\\nNPCScale=nan\\nSpeedMultiplier=9\\nDismountHoldSeconds=-4\\nDismountKey=999999999999\\nAllowJump=garbage\\n";}
 loaded=Settings::Read("build/config-invalid.ini");assert(loaded.npcScale==1&&loaded.speedMultiplier==2&&loaded.holdSeconds==.25f&&loaded.dismountKey==255&&loaded.allowJump);
 assert(!Settings::Write("missing-config-dir/no.ini",cfg));
 std::puts("PASS: actual INI read/write/limits, NaN/invalid values, strict hold threshold, release/reset and 30/60/144 FPS.");
}
`);
