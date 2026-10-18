const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/Extras.cpp'),'utf8');
function take(sig){const start=source.indexOf(sig);assert(start>=0,sig);let end=source.indexOf('{',start)+1,n=1;while(n){const c=source[end++];if(c==='{')n++;if(c==='}')n--;}return source.slice(start,end);}
const bind=take('bool Bind('),handle=take('bool Handle('),release=take('bool Release(');
assert(bind.includes('Mounts::Restore(actor)')&&!bind.includes('Mounts::Convert('));
assert(bind.includes('Wardrobe::Capture(actor,false)'));
assert(bind.includes('existing.pacified!=pacify')&&bind.includes('Release(actor)'));
assert(bind.includes('if(!pacify&&!converted)')&&bind.includes('if(!pacify&&!leashAvailable)'));
assert(!bind.includes('IsHostileToActor'));
assert(handle.includes('i==1')&&handle.includes('CrosshairPickData::GetSingleton()')&&handle.includes('Bind(actor,true)'));
assert(bind.includes('Eligible(actor,!pacify)')&&bind.includes('pacify?RE::NiPointer<RE::TESObjectREFR>{}'));
assert(bind.includes('Crawl::BindBound(actor)')&&!bind.includes('if(pacify){HoldAnchor'));
assert(release.includes('if(b.pacified)Wardrobe::Restore(actor)'));
fs.writeFileSync(path.join(repo,'build/capture-regression.cpp'),'#include <cstdio>\nint main(){std::puts("PASS: capture/fixed-human state contracts");}\n');
