#pragma once
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace Settings {
struct Data {
    bool overrideScale = false;
    float npcScale = 1.f, speedMultiplier = 1.f;
    float holdSeconds = 0.25f, dismountSeconds = 0.85f;
    float mountSeconds = .85f, jumpHeightMultiplier = 1.f;
    float seatHeight = 12.f, seatForward = 0.f, seatSide = 0.f;
    float headingDegrees = 0.f, runThreshold = 180.f, sprintThreshold = 450.f;
    bool allowJump = true, allowBackward = false, blockSneak = true, blockAlt = true, blockCombat = true;
    bool debugLog = false;
    int dismountKey = 0;
};
struct FloatField { const char* key; const char* label; float Data::*member; float min, max; };
inline constexpr FloatField floats[] = {
    {"NPCScale", "NPC 缩放", &Data::npcScale, .5f, 3.f},
    {"SpeedMultiplier", "移动速度倍率", &Data::speedMultiplier, 1.f, 2.f},
    {"DismountHoldSeconds", "下骑长按门槛（秒，必须超过）", &Data::holdSeconds, .25f, 2.f},
    {"DismountAnimationSeconds", "下骑动画时长（秒）", &Data::dismountSeconds, .4f, 2.f},
    {"MountAnimationSeconds", "上骑动画时长（秒）", &Data::mountSeconds, .4f, 2.f},
    {"JumpHeightMultiplier", "骑乘跳跃高度系数（步长0.1）", &Data::jumpHeightMultiplier, .8f, 2.f},
    {"SeatHeight", "骨盆距背部高度（NPC 模型单位）", &Data::seatHeight, 0.f, 60.f},
    {"SeatForward", "座位前后偏移", &Data::seatForward, -50.f, 50.f},
    {"SeatSide", "座位左右偏移", &Data::seatSide, -30.f, 30.f},
    {"HeadingDegrees", "NPC 朝向修正（度）", &Data::headingDegrees, -180.f, 180.f},
    {"RunThreshold", "奔跑动作切换速度", &Data::runThreshold, 40.f, 600.f},
    {"SprintThreshold", "冲刺动作切换速度", &Data::sprintThreshold, 80.f, 1200.f}
};
struct BoolField { const char* key; const char* label; bool Data::*member; };
inline constexpr BoolField bools[] = {
    {"OverrideNPCScale", "覆盖已转换 NPC 的缩放（关闭则保留原值）", &Data::overrideScale},
    {"AllowJump", "允许跳跃", &Data::allowJump},
    {"AllowBackward", "允许后退", &Data::allowBackward},
    {"BlockSneak", "阻止潜行 / Ctrl", &Data::blockSneak},
    {"BlockAlt", "阻止 Alt 动作", &Data::blockAlt},
    {"BlockCombat", "阻止战斗动作（人魅-解除坐骑化与束缚除外）", &Data::blockCombat},
    {"DebugLog", "记录限量骑乘诊断", &Data::debugLog}
};
inline void Normalize(Data& value) {
    const Data defaults;
    for (const auto& field : floats) {
        auto& n = value.*field.member;
        n = std::isfinite(n) ? std::clamp(n, field.min, field.max) : defaults.*field.member;
    }
    value.sprintThreshold = std::max(value.sprintThreshold, value.runThreshold + 20.f);
    value.dismountKey = std::clamp(value.dismountKey, 0, 255);
    value.jumpHeightMultiplier=std::round(value.jumpHeightMultiplier*10.f)/10.f;
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
