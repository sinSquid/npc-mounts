const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/Config.cpp'),'utf8');
function extract(sig){const start=source.indexOf(sig);assert(start>=0);let end=source.indexOf('{',start)+1,depth=1;while(depth){const c=source[end++];if(c==='{')depth++;if(c==='}')depth--;}return source.slice(start,end);}
assert.match(extract('bool Write('), /out\.close\(\);\s*return out\.good\(\)/, 'Save must include close errors in its result');
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
 cfg.logLevel=4;
 Settings::Normalize(cfg);assert(cfg.npcScale==.5f&&cfg.speedMultiplier==1.25f);
 assert(Settings::Write("build/config-roundtrip.ini",cfg));auto loaded=Settings::Read("build/config-roundtrip.ini");
 for(auto f:Settings::floats)assert(cfg.*f.member==loaded.*f.member);
 for(auto f:Settings::bools)assert(cfg.*f.member==loaded.*f.member);
 assert(loaded.dismountKey==18&&loaded.logLevel==4);
 bool success=true;Settings::Read("build/no-such-config-file.ini",&success);assert(!success);
 {std::ofstream f("build/config-bom.ini",std::ios::binary);f<<"\\xEF\\xBB\\xBF"<<"NPCScale=2.5\\nSpeedMultiplier=1.5\\n";}
 loaded=Settings::Read("build/config-bom.ini",&success);assert(success&&loaded.npcScale==2.5f&&loaded.speedMultiplier==1.5f);
 {std::ofstream f("build/config-invalid.ini");f<<"[Mounts]\\nNPCScale=nan\\nSpeedMultiplier=9\\nDismountHoldSeconds=-4\\nDismountKey=999999999999\\nAllowJump=garbage\\n";}
 loaded=Settings::Read("build/config-invalid.ini");assert(loaded.npcScale==1&&loaded.speedMultiplier==2&&loaded.holdSeconds==.25f&&loaded.dismountKey==255&&loaded.allowJump);
 {std::ofstream f("build/config-legacy-on.ini");f<<"DebugLog=1\\n";}assert(Settings::Read("build/config-legacy-on.ini").logLevel==4);
 {std::ofstream f("build/config-legacy-off.ini");f<<"DebugLog=0\\n";}assert(Settings::Read("build/config-legacy-off.ini").logLevel==0);
 {std::ofstream f("build/config-loglevel-wins.ini");f<<"DebugLog=1\\nLogLevel=2\\n";}assert(Settings::Read("build/config-loglevel-wins.ini").logLevel==2);
 cfg.logLevel=99;Settings::Normalize(cfg);assert(cfg.logLevel==4);cfg.logLevel=-1;Settings::Normalize(cfg);assert(cfg.logLevel==0);
 assert(!Settings::Write("missing-config-dir/no.ini",cfg));
 for(auto f:Settings::floats){assert(f.step>=.05f);for(int i=0;i<=1000;i++){float input=f.min+(f.max-f.min)*i/1000.f;float snapped=Settings::Snap(input,f);assert(snapped>=f.min&&snapped<=f.max);assert(Settings::Snap(snapped,f)==snapped);float grid=(snapped-f.min)/f.step;assert(std::abs(grid-std::round(grid))<.001f);}assert(Settings::Snap(f.min,f)==f.min&&Settings::Snap(f.max,f)==f.max);}
 Settings::Data originalDefaults;auto snappedDefaults=originalDefaults;Settings::Normalize(snappedDefaults);for(auto f:Settings::floats)assert(originalDefaults.*f.member==snappedDefaults.*f.member);
 assert(!originalDefaults.undressOnConvert);
 assert(originalDefaults.mouseSteering);
 for(bool enabled:{false,true}){cfg.mouseSteering=enabled;assert(Settings::Write("build/config-mouse.ini",cfg));assert(Settings::Read("build/config-mouse.ini").mouseSteering==enabled);}
 for(bool undress:{false,true}){cfg.undressOnConvert=undress;assert(Settings::Write("build/config-undress.ini",cfg));assert(Settings::Read("build/config-undress.ini").undressOnConvert==undress);}
 for(float v:{-1.f,.8f,1.f,1.24f,1.26f,2.f,3.f,3.7f,4.f,9.f}){cfg.jumpHeightMultiplier=v;Settings::Normalize(cfg);assert(cfg.jumpHeightMultiplier>=1.f&&cfg.jumpHeightMultiplier<=4.f);assert(std::abs(cfg.jumpHeightMultiplier*10-std::round(cfg.jumpHeightMultiplier*10))<.0001f);assert(Settings::Write("build/config-roundtrip.ini",cfg));assert(Settings::Read("build/config-roundtrip.ini").jumpHeightMultiplier==cfg.jumpHeightMultiplier);}
 std::puts("PASS: actual INI read/write/limits, NaN/invalid values, strict hold threshold, release/reset and 30/60/144 FPS.");
}
`);
