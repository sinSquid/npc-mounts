#pragma once
#include <cmath>

namespace Motion {
// Different enter/leave thresholds keep a nearly constant speed from restarting clips.
inline int Choose(float speed, float forward, float yawRate, int& gait)
{
    if (speed < 2.f) gait = 0;
    else if (speed >= 450.f || (gait == 3 && speed >= 420.f)) gait = 3;
    else if (speed >= 180.f || (gait >= 2 && speed >= 160.f)) gait = 2;
    else gait = 1;
    if (gait && forward < -0.1f) return 4;
    if (!gait && std::abs(yawRate) > 0.6f) return yawRate < 0.f ? 5 : 6;
    return gait;
}
}
