#pragma once
namespace Crawl {
bool Install(const char* path="Data/SKSE/Plugins/NPCMounts/Crawl.nmc");
bool Available();
void GeneratePose(RE::hkbGenerator*,const RE::hkbContext&,const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float);
bool Bind(RE::Actor* actor,bool publish=true);
bool BindBound(RE::Actor* actor);
bool Resume(RE::Actor* actor);
void Update(RE::Actor* actor,int gait,float delta);
void UpdateBound(RE::Actor* actor,float delta);
bool Anchor(RE::Actor* actor,RE::NiPoint3& point,float sway);
bool Working(RE::Actor* actor);
void LogOutput();
void Remove(RE::FormID id);
void Clear();
}
