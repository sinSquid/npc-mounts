#include "pch.h"
#include "Config.h"
#include <charconv>
#include <fstream>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <limits>

namespace Settings {
namespace { std::mutex mutex; Data current; std::string status = "尚未进入骑乘"; }
Data Read(const std::filesystem::path& path, bool* success) {
    Data result;
    std::ifstream in(path);
    if(success)*success=false;
    if(!in)return result;
    std::string line;
    while (std::getline(in, line)) {
        if(line.starts_with("\xEF\xBB\xBF"))line.erase(0,3);
        const auto comment = line.find_first_of(";#"); if (comment != std::string::npos) line.resize(comment);
        const auto equals = line.find('='); if (equals == std::string::npos) continue;
        std::string key = line.substr(0, equals), text = line.substr(equals + 1);
        const auto trim = [](std::string& s) { const auto first=s.find_first_not_of(" \t\r"); if(first==std::string::npos){s.clear();return;} s=s.substr(first,s.find_last_not_of(" \t\r")-first+1); };
        trim(key); trim(text);
        float number{}; auto parsed = std::from_chars(text.data(), text.data()+text.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size()) continue;
        for (const auto& field : floats) if (key == field.key) result.*field.member = number;
        for (const auto& field : bools) if (key == field.key && (number == 0.f || number == 1.f)) result.*field.member = number != 0.f;
        if (key == "DismountKey" && std::isfinite(number)) result.dismountKey = static_cast<int>(std::clamp(number,0.f,255.f));
    }
    Normalize(result); if(success)*success=!in.bad(); return result;
}
bool Write(const std::filesystem::path& path, Data value) {
    Normalize(value);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "; NPCMounts 0.4.2 - UTF-8; menu and INI use the same limits\n[Mounts]\n";
    for (const auto& f : bools) out << f.key << '=' << (value.*f.member ? 1 : 0) << '\n';
    for (const auto& f : floats) {
        out << std::setprecision(6) << "; range " << f.min << ".." << f.max << "; step " << f.step << '\n';
        out << std::setprecision(std::numeric_limits<float>::max_digits10) << f.key << '=' << value.*f.member << '\n';
    }
    out << "; 0 = current Activate binding; other values = keyboard scan code (decimal)\nDismountKey=" << value.dismountKey << '\n';
    out.flush(); return out.good();
}
Data Get() { std::scoped_lock lock(mutex); return current; }
void Set(Data value) { Normalize(value); std::scoped_lock lock(mutex); current=value; }
void Status(std::string text) { std::scoped_lock lock(mutex); status=std::move(text); }
void Initialize() { Set(Read(iniPath)); }

// Minimal dynamic ABI declarations, matching SKSEMenuFramework.h supplied by
// SKSE Menu Framework. No private ImGui structs or hard dependency on its DLL.
namespace Menu {
struct Vec2 { float x,y; };
void (*text)(const char*, const char*){};
bool (*slider)(const char*, float*, float, float, const char*, int){};
bool (*checkbox)(const char*, bool*){};
bool (*button)(const char*, Vec2){};
bool (*inputInt)(const char*, int*, int, int, int){};
void (*separator)(const char*){};
void (__stdcall Render)() {
    static Data draft = Get();
    static std::string result;
    text("NPC 坐骑 0.4.2 · 修改后点击保存并应用", nullptr);
    text("下骑恢复玩家速度；人魅-解除坐骑化与束缚恢复 NPC 原值。", nullptr);
    for (const auto& f : bools) checkbox(f.label, &(draft.*f.member));
    separator("数值与座位");
    for (const auto& f : floats) {
        const auto label=fmt::format("{}（步长{:g}）###{}",f.label,f.step,f.key);
        slider(label.c_str(), &(draft.*f.member), f.min, f.max, f.step<.1f?"%.2f":f.step<1.f?"%.1f":"%.0f", 0);
    }
    Normalize(draft); // Same grid and cross-field constraints as INI read/write.
    inputInt("下骑键扫描码（0=游戏交互键）", &draft.dismountKey, 1, 10, 0);
    if (button("保存并应用", {0,0})) {
        Normalize(draft);
        if (Write(iniPath,draft)) { Set(draft); result="已保存 INI，游戏继续后应用"; }
        else result="写入 INI 失败；未应用，请检查文件权限";
    }
    if (button("从 INI 重新读取并应用", {0,0})) {
        bool success=false;const auto loaded=Read(iniPath,&success);
        if(success){draft=loaded;Set(draft);result="已重新读取 INI";}
        else result="读取 INI 失败；保留当前配置与未保存修改";
    }
    if (button("恢复默认值（尚未保存）", {0,0})) { draft=Data{}; result="默认值已填入，点击保存并应用生效"; }
    text(result.c_str(),nullptr);
    std::string state; { std::scoped_lock lock(mutex); state=status; }
    separator("当前状态"); text(state.c_str(),nullptr);
}
}
void InstallMenu() {
    const auto module=GetModuleHandleW(L"SKSEMenuFramework.dll");
    if (!module) { spdlog::info("Menu Framework absent; INI configuration remains available"); return; }
    auto add=reinterpret_cast<void(*)(const char*,void(__stdcall*)())>(GetProcAddress(module,"AddSectionItem"));
#define LOAD_MENU(field, name) Menu::field=reinterpret_cast<decltype(Menu::field)>(GetProcAddress(module,name))
    LOAD_MENU(text,"igTextUnformatted"); LOAD_MENU(slider,"igSliderFloat"); LOAD_MENU(checkbox,"igCheckbox");
    LOAD_MENU(button,"igButton"); LOAD_MENU(inputInt,"igInputInt"); LOAD_MENU(separator,"igSeparatorText");
#undef LOAD_MENU
    if (!add || !Menu::text || !Menu::slider || !Menu::checkbox || !Menu::button || !Menu::inputInt || !Menu::separator) {
        spdlog::error("Menu Framework API incomplete; INI still available"); return;
    }
    add("NPC坐骑/骑乘配置",Menu::Render);
    spdlog::info("Registered Chinese Menu Framework configuration page");
}
}
