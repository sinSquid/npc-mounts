#pragma once
#include <algorithm>
struct HoldGate {
    bool pressed=false;float elapsed=0;
    void Press(){pressed=true;elapsed=0;}
    void Release(){pressed=false;elapsed=0;}
    bool Tick(float delta,float threshold){
        if(!pressed)return false;
        elapsed+=std::max(delta,0.f);
        if(elapsed<=threshold)return false;
        Release();return true;
    }
};
