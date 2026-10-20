#pragma once
#include <cstdint>
// Engine-double suites ignore logging; production logging is tested separately.
namespace Diagnostics {
struct Context{std::uint64_t operation=0;std::uint32_t actor=0;};
inline Context current;
struct Scope{explicit Scope(Context){} explicit Scope(std::uint32_t){}};
template<class...T>void Trace(const char*,T&&...){}
template<class...T>void Motion(const char*,T&&...){}
inline void Flush(){}
template<class T>void Snapshot(T*,const char*){}
inline bool Sample(unsigned,std::uint64_t=500){return true;}
}
// WaterRide itself is executed by water-ride.cpp; other suites isolate its effects.
namespace WaterRide {
template<class... T>void Begin(T...){}
template<class... T>void End(T...){}
template<class... T>void Remove(T...){}
template<class... T>void Recover(T...){}
template<class... T>void Forget(T...){}
template<class... T>void Update(T...){}
template<class... T>void Save(T...){}
template<class... T>bool Load(T...){return false;}
inline float Offset(){return 0.f;}
inline void Reset(){}
}
