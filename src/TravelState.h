#pragma once
#include <algorithm>
#include <cstdint>

namespace Travel {
struct Location {
    std::uint32_t cell=0,world=0;
    bool interior=false;
    float x=0,y=0,z=0;
};
// Runtime observation only: never persist a previous save's player position.
struct Tracker {
    Location previous;
    bool initialized=false,gap=false,pending=false;
    float settle=0,remaining=0;
    void Gap(){if(initialized)gap=true;settle=0;}
    void Arrived(){if(initialized){pending=true;settle=0;remaining=10.f;}}
    bool Observe(Location now,float delta){
        if(!initialized){previous=now;initialized=true;gap=false;return false;}
        const float x=now.x-previous.x,y=now.y-previous.y,z=now.z-previous.z;
        const float distance=x*x+y*y+z*z;
        const bool region=now.world!=previous.world||
            (now.cell!=previous.cell&&(now.interior||previous.interior));
        // Exterior grid boundaries during ordinary walking are not teleports.
        const bool moved=region||distance>1024.f*1024.f||(gap&&distance>128.f*128.f);
        previous=now;gap=false;
        if(moved)Arrived();
        if(pending){const float dt=std::clamp(delta,0.f,.1f);settle+=dt;remaining-=dt;if(remaining<=0)pending=false;}
        return pending&&settle>=.75f;
    }
    void Done(){pending=false;settle=remaining=0;}
};
}
