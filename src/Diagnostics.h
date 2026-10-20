#pragma once
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
namespace Diagnostics {
struct Context {std::uint64_t operation=0;std::uint32_t actor=0;};
inline std::atomic_uint64_t sequence{0};
inline thread_local Context current;
struct Scope {
    Context previous=current;
    explicit Scope(Context context){current=context;}
    explicit Scope(std::uint32_t actor):Scope(Context{++sequence,actor}){}
    ~Scope(){current=previous;}
};
template<class... Args> void Trace(fmt::format_string<Args...> format,Args&&... args){
    if(!spdlog::default_logger()->should_log(spdlog::level::info))return;
    spdlog::info("op={} ref={:08X} {}",current.operation,current.actor,fmt::format(format,std::forward<Args>(args)...));
}
template<class... Args> void Motion(fmt::format_string<Args...> format,Args&&... args){if(spdlog::default_logger()->should_log(spdlog::level::debug))spdlog::debug(format,std::forward<Args>(args)...);}
inline void Flush(){spdlog::default_logger()->flush();}
inline bool Sample(unsigned slot,std::uint64_t interval=500){
    static std::array<std::atomic_uint64_t,6> gates{};
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto& gate=gates.at(slot);
    auto next=gate.load(std::memory_order_relaxed);
    while(static_cast<std::uint64_t>(now)>=next)
        if(gate.compare_exchange_weak(next,static_cast<std::uint64_t>(now)+interval,std::memory_order_relaxed))return true;
    return false;
}
inline std::mutex loggerMutex;
inline std::filesystem::path logDirectory;
inline std::shared_ptr<spdlog::logger> fileLogger;
inline std::shared_ptr<spdlog::logger> nullLogger;
inline int configuredLevel=0;
inline spdlog::level::level_enum SpdLevel(int level){
    return level>=4?spdlog::level::debug:level==3?spdlog::level::info:level==2?spdlog::level::warn:level==1?spdlog::level::err:spdlog::level::off;
}
inline void SetLevel(int level){
    level=std::clamp(level,0,4);std::scoped_lock lock(loggerMutex);configuredLevel=level;
    if(level==0){
        if(!nullLogger)nullLogger=std::make_shared<spdlog::logger>("NPCMounts-off",std::make_shared<spdlog::sinks::null_sink_mt>());
        nullLogger->set_level(spdlog::level::off);spdlog::set_default_logger(nullLogger);return;
    }
    if(!fileLogger&&!logDirectory.empty()){
        try {
        SYSTEMTIME time{};GetLocalTime(&time);
        const auto run=fmt::format("{:04}{:02}{:02}-{:02}{:02}{:02}-{:03}-{}-{}",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,GetCurrentProcessId(),GetTickCount64());
        const auto archive=logDirectory/"NPCMounts-runs";
        std::error_code error;std::filesystem::create_directories(archive,error);
        auto path=error?logDirectory/("NPCMounts-"+run+".log"):archive/(run+".log");
        auto history=std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(),false);
        auto latest=std::make_shared<spdlog::sinks::basic_file_sink_mt>((logDirectory/"NPCMounts.log").string(),true);
        fileLogger=std::make_shared<spdlog::logger>("NPCMounts",spdlog::sinks_init_list{history,latest});
        fileLogger->flush_on(spdlog::level::info);
        fileLogger->set_level(SpdLevel(level));spdlog::set_default_logger(fileLogger);
        fileLogger->info("op=0 ref=00000000 Session={} archive={} build={} {}",run,path.string(),__DATE__,__TIME__);return;
        } catch(const spdlog::spdlog_ex&) {
            fileLogger.reset();
            if(!nullLogger)nullLogger=std::make_shared<spdlog::logger>("NPCMounts-off",std::make_shared<spdlog::sinks::null_sink_mt>());
            nullLogger->set_level(spdlog::level::off);spdlog::set_default_logger(nullLogger);return;
        }
    }
    if(fileLogger){fileLogger->set_level(SpdLevel(level));spdlog::set_default_logger(fileLogger);}
}
inline void Initialize(const std::filesystem::path& directory,int level){
    {std::scoped_lock lock(loggerMutex);
        if(!nullLogger)nullLogger=std::make_shared<spdlog::logger>("NPCMounts-off",std::make_shared<spdlog::sinks::null_sink_mt>());
        nullLogger->set_level(spdlog::level::off);spdlog::set_default_logger(nullLogger);
        fileLogger.reset();logDirectory=directory;
    }
    SetLevel(level);
}
inline void Snapshot(RE::Actor* actor,const char* event){
    if(!spdlog::default_logger()->should_log(spdlog::level::info))return;
    if(!actor){Trace("{} target=null",event);return;}
    const auto* base=actor->GetActorBase();const auto* race=actor->GetRace();const auto* cell=actor->GetParentCell();
    const auto* name=actor->GetName();
    const auto position=actor->GetPosition();auto* player=RE::PlayerCharacter::GetSingleton();
    Trace("{} target={:08X} base={:08X} name={} race={:08X} cell={:08X} scale={} speed={} AI={} life={} 3D={} controller={} hostile={} pos=({},{},{})",
        event,actor->GetFormID(),base?base->GetFormID():0,name?name:"",race?race->GetFormID():0,cell?cell->GetFormID():0,
        actor->GetScale(),actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeedMult),actor->IsAIEnabled(),
        static_cast<unsigned>(actor->AsActorState()->GetLifeState()),actor->Get3D()!=nullptr,actor->GetCharController()!=nullptr,
        player&&actor->IsHostileToActor(player),position.x,position.y,position.z);
    RE::NiPointer<RE::Actor> nativeMount;
    Trace("{} eligibility spine={} killmove={} furnitureHandle={} interactionMounted={} sit={} nativeMounted={}",event,
        actor->Get3D()&&actor->Get3D()->GetObjectByName("NPC Spine2 [Spn2]"),actor->IsInKillMove(),bool(actor->GetOccupiedFurniture()),actor->IsOnMount(),
        static_cast<unsigned>(actor->AsActorState()->GetSitSleepState()),actor->GetMount(nativeMount));
}
}
