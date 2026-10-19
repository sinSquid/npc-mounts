#pragma once
#include "Config.h"
namespace Seat {
bool Install();
void GeneratePose(RE::hkbGenerator*,const RE::hkbContext&,const RE::hkbGeneratorOutput**,RE::hkbGeneratorOutput&,float);
bool Bind(RE::Actor* player);
bool Refresh(RE::Actor* player);
const char* BindError();
void Clear();
void Motion(float delta,bool airborne,int gait);
void Update(RE::Actor* player, RE::Actor* mount, const Settings::Data& config,
    float dismountProgress = -1.f, RE::NiPoint3 exitOffset = {});
bool Working();
unsigned TakeWrites();
void LogOutput();
bool Finished();
void Entering(bool value);
}
