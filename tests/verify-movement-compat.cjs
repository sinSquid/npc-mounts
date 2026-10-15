const fs=require('fs'),path=require('path');
fs.writeFileSync(path.join(__dirname,'../build/movement-compat-regression.cpp'),`
#include <cstdint>
#include <cassert>
#include <cstdio>
namespace RE{using ActorHandle=unsigned;}
namespace SKSE{using PluginHandle=unsigned;unsigned GetPluginHandle(){return 7;}}
void* GetModuleHandleW(const wchar_t*){return nullptr;}void* GetProcAddress(void*,const char*){return nullptr;}
namespace spdlog{template<class...T>void info(const char*,T...){}}
#include "../src/MovementCompat.h"
struct Mock:MovementCompat::Interface {
 using R=MovementCompat::Result;R reply=R::OK;unsigned releases=0;
 unsigned long GetTDMThreadId()const noexcept override{return 0;}
 bool GetDirectionalMovementState()const noexcept override{return true;}
 bool GetTargetLockState()const noexcept override{return false;}
 unsigned GetCurrentTarget()const noexcept override{return 0;}
 R RequestDisableDirectionalMovement(unsigned id)noexcept override{assert(id==7);return reply;}
 R RequestDisableHeadtracking(unsigned)noexcept override{assert(false);return R::OK;}
 unsigned GetDisableDirectionalMovementOwner()const noexcept override{return 7;}
 unsigned GetDisableHeadtrackingOwner()const noexcept override{return 0;}
 R ReleaseDisableDirectionalMovement(unsigned id)noexcept override{assert(id==7);++releases;return R::OK;}
 R ReleaseDisableHeadtracking(unsigned)noexcept override{assert(false);return R::OK;}
};
int main(){using namespace MovementCompat;Initialize();assert(!api&&Begin());End();Mock mock;api=&mock;
 for(int i=0;i<1000;i++){assert(Begin()&&owned);End();assert(!owned);End();}assert(mock.releases==1000);
 mock.reply=Result::AlreadyTaken;assert(Begin()&&!owned);End();assert(mock.releases==1000);
 mock.reply=Result::MustKeep;assert(!Begin()&&!owned);End();assert(mock.releases==1000);
 mock.reply=Result::AlreadyGiven;assert(Begin()&&owned);End();assert(mock.releases==1001);
 puts("PASS: optional TDM V1 handoff, 1000 releases, missing API, refused request and other-owner isolation; not engine QA.");}
`);
