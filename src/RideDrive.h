#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace RideDrive {
inline bool left=false,right=false;
inline float stick=0.f;
inline bool owned=false;
inline void ResetInput(){left=right=false;stick=0.f;}
inline float Turn(float delta){return std::clamp(float(right)-float(left)+stick,-1.f,1.f)*1.5f*std::clamp(delta,0.f,.1f);}
inline void Aim(RE::Actor* player,float& cameraYaw,float delta){
    if(!owned||left||right||stick!=0.f)return;
    const float limit=1.5f*std::clamp(delta,0.f,.1f);
    const float turn=std::clamp(std::remainder(cameraYaw,6.28318530718f),-limit,limit);
    player->SetHeading(player->GetAngleZ()+turn);
    // Preserve world camera direction while the actor catches up (TDM convention).
    cameraYaw-=turn;
}
inline void Begin(){
    if(owned)return;
    owned=true;ResetInput();
}
inline void End(){
    owned=false;ResetInput();
}
inline void Before(RE::Actor* player,float delta,bool locked){
    if(!owned)return;
    auto& data=RE::PlayerControls::GetSingleton()->data;
    // POV-slide is the engine's stationary inspection mode, not riding free-look.
    // ThirdPersonState::SetFreeRotationMode is handled separately by RideHooks.
    data.moveInputVec.x=0.f;data.prevMoveVec.x=0.f;
    if(!locked){
        const float turn=Turn(delta);
        if(turn!=0.f)player->SetHeading(player->GetAngleZ()+turn);
    }
}
}
