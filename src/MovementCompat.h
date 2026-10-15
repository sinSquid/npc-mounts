#pragma once
// Optional TDM V1 ABI, kept in the published virtual-method order.
// https://github.com/ersh1/TrueDirectionalMovement/blob/master/src/TrueDirectionalMovementAPI.h
namespace MovementCompat {
enum class Result : std::uint8_t { OK,NotOwner,MustKeep,AlreadyGiven,AlreadyTaken,BadThread };
class Interface {
public:
    virtual unsigned long GetTDMThreadId() const noexcept=0;
    virtual bool GetDirectionalMovementState() const noexcept=0;
    virtual bool GetTargetLockState() const noexcept=0;
    virtual RE::ActorHandle GetCurrentTarget() const noexcept=0;
    virtual Result RequestDisableDirectionalMovement(SKSE::PluginHandle) noexcept=0;
    virtual Result RequestDisableHeadtracking(SKSE::PluginHandle) noexcept=0;
    virtual SKSE::PluginHandle GetDisableDirectionalMovementOwner() const noexcept=0;
    virtual SKSE::PluginHandle GetDisableHeadtrackingOwner() const noexcept=0;
    virtual Result ReleaseDisableDirectionalMovement(SKSE::PluginHandle) noexcept=0;
    virtual Result ReleaseDisableHeadtracking(SKSE::PluginHandle) noexcept=0;
};
inline Interface* api=nullptr;
inline bool owned=false;
inline void Initialize(){
    auto module=GetModuleHandleW(L"TrueDirectionalMovement.dll");
    if(!module)return;
    auto request=reinterpret_cast<void*(*)(std::uint8_t)>(GetProcAddress(module,"RequestPluginAPI"));
    if(request)api=static_cast<Interface*>(request(0)); // V1, not the newest ABI.
    spdlog::info("Optional TDM movement interface available={}",api!=nullptr);
}
inline bool Begin(){
    if(!api)return true;
    const auto result=api->RequestDisableDirectionalMovement(SKSE::GetPluginHandle());
    owned=result==Result::OK||result==Result::AlreadyGiven;
    spdlog::info("TDM riding handoff result={}",static_cast<unsigned>(result));
    // Another owner may already have disabled TDM. Never release its request.
    return owned||result==Result::AlreadyTaken;
}
inline void End(){
    if(api&&owned)api->ReleaseDisableDirectionalMovement(SKSE::GetPluginHandle());
    owned=false;
}
}
