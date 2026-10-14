#pragma once
#include <cmath>

namespace Motion {
// Different enter/leave thresholds keep a nearly constant speed from restarting clips.
inline int Choose(float speed, float forward, float yawRate, int& gait,float run=180.f,float sprint=450.f)
{
    if (speed < 2.f) gait = 0;
    else if (speed >= sprint || (gait == 3 && speed >= sprint-30.f)) gait = 3;
    else if (speed >= run || (gait >= 2 && speed >= run-20.f)) gait = 2;
    else gait = 1;
    if (gait && forward < -0.1f) return 4;
    if (!gait && std::abs(yawRate) > 0.6f) return yawRate < 0.f ? 5 : 6;
    return gait;
}
}
