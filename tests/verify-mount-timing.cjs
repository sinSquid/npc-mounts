const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/plugin.cpp'),'utf8');
const mount=source.slice(source.indexOf('void Mount('),source.indexOf('void Tick('));
assert(mount.includes('upActive=true;transitionTime=0.f'));
assert(mount.includes('Seat::Entering(true);Seat::Update(player,actor,config,1.f,downOffset)'));
assert(mount.includes('ResetPose(actor,targets.at(actor->GetFormID()))'));
assert(mount.includes('cc->outVelocity=RE::hkVector4()')&&mount.includes('proxy->outVelocity=RE::hkVector4()'));
assert(!mount.includes('NotifyAnimationGraph("Mount')&&!mount.includes('NotifyAnimationGraph("Horse'));
fs.writeFileSync(path.join(repo,'build/mount-timing-regression.cpp'),'#include <cstdio>\nint main(){std::puts("PASS: mount transition source contract");}\n');
