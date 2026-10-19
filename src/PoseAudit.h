#pragma once
#include <array>
#include <cstdint>
// Protected by the existing pose mutex. Logging occurs only in the 2 Hz debug sample.
struct PoseAudit {
    std::uint64_t calls=0,matched=0,writes=0;
    std::array<std::uint64_t,7> rejected{};
    int count=0,capacity=0,flags=0,type=0,bytes=0,offset=0,bound=0;
    float fraction=0;
    void Reject(unsigned reason){++rejected[reason];}
    template<class Track> void TrackInfo(const Track& track,int boundCount){
        count=track.numData;capacity=track.capacity;flags=static_cast<int>(track.flags);
        type=static_cast<int>(track.type);bytes=track.elementSizeBytes;offset=track.dataOffset;
        fraction=track.onFraction;bound=boundCount;
    }
};
