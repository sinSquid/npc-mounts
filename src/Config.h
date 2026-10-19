#pragma once
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace Settings {
struct Data {
    bool overrideScale = false;
    float npcScale = 1.f, speedMultiplier = 1.f;
    float holdSeconds = 0.25f;
    float jumpHeightMultiplier = 1.f;
    float seatHeight = 12.f, seatForward = 0.f, seatSide = 0.f, riderSway = .35f;
    float headingDegrees = 0.f, runThreshold = 180.f, sprintThreshold = 450.f;
    bool allowJump = true, allowBackward = false, blockSneak = true, blockAlt = true, blockCombat = true;
    int logLevel = 0;
    bool undressOnConvert = false;
    bool mouseSteering = true;
    int dismountKey = 0;
};
struct FloatField { const char* key; const char* label; float Data::*member; float min, max, step; };
inline constexpr FloatField floats[] = {
    {"NPCScale", "NPC 缩放", &Data::npcScale, .5f, 3.f, .05f},
    {"SpeedMultiplier", "移动速度倍率", &Data::speedMultiplier, 1.f, 2.f, .05f},
    {"DismountHoldSeconds", "下骑长按门槛（秒，必须超过）", &Data::holdSeconds, .25f, 2.f, .05f},
    {"JumpHeightMultiplier", "骑乘跳跃高度系数", &Data::jumpHeightMultiplier, 1.f, 4.f, .1f},
    {"SeatHeight", "骨盆距背部高度（NPC 模型单位）", &Data::seatHeight, 0.f, 60.f, .5f},
    {"SeatForward", "座位前后偏移", &Data::seatForward, -50.f, 50.f, .5f},
    {"SeatSide", "座位左右偏移", &Data::seatSide, -30.f, 30.f, .5f},
    {"RiderSway", "骑手颠簸强度", &Data::riderSway, 0.f, 1.f, .05f},
    {"HeadingDegrees", "NPC 朝向修正（度）", &Data::headingDegrees, -180.f, 180.f, 1.f},
    {"RunThreshold", "奔跑动作切换速度", &Data::runThreshold, 40.f, 600.f, 5.f},
    {"SprintThreshold", "冲刺动作切换速度", &Data::sprintThreshold, 80.f, 1200.f, 5.f}
};
struct BoolField { const char* key; const char* label; bool Data::*member; };
inline constexpr BoolField bools[] = {
    {"MouseSteering", "鼠标准星控制转向（关闭则鼠标仅观察）", &Data::mouseSteering},
    {"UndressOnConvert", "坐骑化时卸下服装与护甲（保留在目标背包）", &Data::undressOnConvert},
    {"OverrideNPCScale", "覆盖已转换 NPC 的缩放（关闭则保留原值）", &Data::overrideScale},
    {"AllowJump", "允许跳跃", &Data::allowJump},
    {"AllowBackward", "允许后退", &Data::allowBackward},
    {"BlockSneak", "阻止潜行 / Ctrl", &Data::blockSneak},
    {"BlockAlt", "阻止 Alt 动作", &Data::blockAlt},
    {"BlockCombat", "阻止战斗动作（人魅-解除坐骑化与束缚除外）", &Data::blockCombat}
};
inline float Snap(float value,const FloatField& field) {
    // All ranges are exact multiples of their step. Round in decimal units
    // to avoid asymmetric half-step decisions from binary float constants.
    const double step=std::round(double(field.step)*100.0)/100.0;
    const double minimum=std::round(double(field.min)*100.0)/100.0;
    const double index=(std::clamp(value,field.min,field.max)-minimum)/step;
    return std::clamp(static_cast<float>(minimum+std::floor(index+.5+1e-5)*step),field.min,field.max);
}
inline void Normalize(Data& value) {
    const Data defaults;
    for (const auto& field : floats) {
        auto& n = value.*field.member;
        n = Snap(std::isfinite(n) ? n : defaults.*field.member,field);
    }
    value.sprintThreshold = std::max(value.sprintThreshold, value.runThreshold + 20.f);
    value.dismountKey = std::clamp(value.dismountKey, 0, 255);
    value.logLevel = std::clamp(value.logLevel, 0, 4);
}
Data Read(const std::filesystem::path& path, bool* success = nullptr);
bool Write(const std::filesystem::path& path, Data value);
Data Get();
void Set(Data value);
void Initialize();
void InstallMenu();
void Status(std::string text);
inline const std::filesystem::path iniPath = "Data/SKSE/Plugins/NPCMounts.ini";
}
