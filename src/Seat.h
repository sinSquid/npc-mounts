#pragma once
#include "Config.h"
namespace Seat {
bool Install();
bool Bind(RE::Actor* player);
void Clear();
void Update(RE::Actor* player, RE::Actor* mount, const Settings::Data& config,
    float dismountProgress = -1.f, RE::NiPoint3 exitOffset = {});
bool Working();
bool Finished();
}
