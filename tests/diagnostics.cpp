#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <Windows.h>
#include <cassert>
#include <fstream>
#include <thread>
#include "../src/Diagnostics.h"
std::string Read(const std::filesystem::path& path){std::ifstream in(path);return {std::istreambuf_iterator<char>(in),{}};}
int main(){
 using namespace Diagnostics;
 std::atomic_uint winners{0};std::vector<std::thread> workers;
 for(unsigned i=0;i<32;++i)workers.emplace_back([&]{for(unsigned j=0;j<1000;++j)if(Sample(2,60000))++winners;});
 for(auto& worker:workers)worker.join();assert(winners==1);
 const auto root=std::filesystem::temp_directory_path()/fmt::format("NPCMounts-diagnostic-test-{}-{}",GetCurrentProcessId(),GetTickCount64());
 const auto off=root/"off";std::filesystem::create_directories(off);Initialize(off,0);Trace("must-not-exist");spdlog::error("must-not-exist");Flush();
 assert(!std::filesystem::exists(off/"NPCMounts.log")&&!std::filesystem::exists(off/"NPCMounts-runs"));
 const auto dir=root/"enabled";std::filesystem::create_directories(dir);Initialize(dir,4);
 Context parent{};
 {Scope op(0x123);parent=current;Trace("operation-before");{Scope nested(0x456);assert(current.operation!=parent.operation);}assert(current.operation==parent.operation);}
 assert(current.operation==0);
 std::thread callback([parent]{Scope op(parent);assert(current.actor==0x123);Trace("callback-same-operation");});callback.join();
 Motion("motion-batch");Flush();const auto old=Read(dir/"NPCMounts.log");assert(old.find("operation-before")!=std::string::npos&&old.find("callback-same-operation")!=std::string::npos&&old.find("motion-batch")!=std::string::npos);
 Sleep(2);Initialize(dir,4);Trace("second-session");Flush();
 unsigned histories=0;bool previousFound=false;
 for(const auto& file:std::filesystem::directory_iterator(dir/"NPCMounts-runs")){++histories;previousFound|=Read(file.path())==old;}
 assert(histories==2&&previousFound);
 const auto latest=Read(dir/"NPCMounts.log");assert(latest.find("second-session")!=std::string::npos&&latest.find("operation-before")==std::string::npos);
 SetLevel(0);Trace("off-after-enable");spdlog::error("off-after-enable");Flush();assert(Read(dir/"NPCMounts.log")==latest);
 SetLevel(3);Trace("info-after-enable");Motion("debug-must-not-exist");Flush();const auto info=Read(dir/"NPCMounts.log");assert(info.find("info-after-enable")!=std::string::npos&&info.find("debug-must-not-exist")==std::string::npos);
 SetLevel(1);spdlog::warn("warn-must-not-exist-level1");spdlog::error("error-level1");Flush();const auto errorOnly=Read(dir/"NPCMounts.log");assert(errorOnly.find("error-level1")!=std::string::npos&&errorOnly.find("warn-must-not-exist-level1")==std::string::npos);
 SetLevel(2);Trace("info-must-not-exist-level2");spdlog::warn("warn-level2");Flush();const auto warning=Read(dir/"NPCMounts.log");assert(warning.find("warn-level2")!=std::string::npos&&warning.find("info-must-not-exist-level2")==std::string::npos);
 std::printf("PASS: level 0 creates/touches no logs; levels reconfigure at runtime; sessions, filtering, operation context and bounded sampling verified. Evidence: %s. Not game QA.\n",root.string().c_str());
}
