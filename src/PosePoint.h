#pragma once
#include <array>
// Model-space point in a sampled pose; never mixes published scene roots from different frames.
template<class Frame,class Parents>
std::array<float,3> PosePoint(const Frame& frame,const Parents& parents,int bone){
    std::array<float,3> p{};
    for(int i=bone;i>=0;i=parents[i]){
        const auto& b=frame[i];const float x=b.q[0],y=b.q[1],z=b.q[2],w=b.q[3];
        const float px=p[0]*b.s[0],py=p[1]*b.s[1],pz=p[2]*b.s[2];
        p={b.t[0]+(1-2*(y*y+z*z))*px+2*(x*y-z*w)*py+2*(x*z+y*w)*pz,
           b.t[1]+2*(x*y+z*w)*px+(1-2*(x*x+z*z))*py+2*(y*z-x*w)*pz,
           b.t[2]+2*(x*z-y*w)*px+2*(y*z+x*w)*py+(1-2*(x*x+y*y))*pz};
    }
    return p;
}
