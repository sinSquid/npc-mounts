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
