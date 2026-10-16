#pragma once
#include <cmath>
#include <algorithm>

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

// Riding is player-driven. Physics correction velocity is not movement intent.
struct RideState {
    int gait=0, animation=0, pending=0;
    float pendingTime=0.f, groundedTime=0.f;
    bool jumping=false;
    int Update(float speed,float x,float y,float yawRate,bool air,bool takeoff,float delta,float run,float sprint) {
        const float dt=std::clamp(delta,0.f,.1f);
        if(takeoff){jumping=true;groundedTime=0.f;}
        if(jumping){
            groundedTime=air?0.f:groundedTime+dt;
            if(groundedTime<.12f){animation=pending=2;pendingTime=0.f;return 2;}
            jumping=false;
        }
        const bool input=x*x+y*y>.01f;
        int wanted=Choose(input?speed:0.f,y,yawRate,gait,run,sprint);
        // Stop immediately; start immediately. Debounce changes between active gaits.
        if(wanted==0||animation==0){animation=pending=wanted;pendingTime=0.f;return animation;}
        if(wanted==animation){pending=wanted;pendingTime=0.f;return animation;}
        if(wanted!=pending){pending=wanted;pendingTime=0.f;}
        pendingTime+=dt;
        if(pendingTime>=.12f){animation=wanted;pendingTime=0.f;}
        return animation;
    }
};
}
