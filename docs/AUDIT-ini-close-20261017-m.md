# 2026-10-17 M：保存、移动、跳跃与还原复核

## 证据与修改范围

- 游戏日志仍停在 03:30:44，未出现 L 部署后的游戏记录。不能用旧日志判断新钩子的游戏效果，也没有新增保存崩溃栈。
- Config.cpp 的 Write 原先在 flush 后读取 good，close 在析构时执行，关闭错误不参与返回结果。现显式 close 后判断 good；新增源函数断言。只修正成功反馈，不声称修复 CTD。既有保存四阶段日志保留。
- 实际安装的 Menu Framework DLL 使用生产 Render/Read/Write，并通过鼠标事件操作真实按钮。独立进程不等于游戏渲染线程、MO2 或其他插件环境。
- 骑乘同步检查到 NPCUpdate → SyncMount → 原 NPC 更新，在更新栈中限制被骑 NPC 的自身位移。玩家原生运动仍保留。没有本轮运行日志可证明所有原生位移均经过该栈，不扩大钩子范围。
- 跳跃检查到 JumpInput 接受原处理，Tick 检测起跳后仅一次高度倍率，Seat 使用骑乘姿态。无证据支持另造跳跃驱动，保持现有实现；独立跳跃仍待游戏验证。
- B 还原检查包括待卸载目标、异步解绳、动画下骑、scale/speed 差量、原 AI/生命状态及读档迁移。实际序列化入口 lambda 已先调用 RideCollision::Clear，再调用 Mounts::Revert，因此内部未重复清理不是 bug；检查中曾误判，已更正且未修改。

## 验证与交付边界

- 两轮 tools/test-native.ps1：各 27 组生产函数离线回归，以及实际 EXE 入口检查和独立 MASM ABI 测试；日志位于 build/ride-m。
- 两轮实际 Menu Framework DLL 保存点击测试，各 50 次，并切换诊断选项、重新读取。保留测试的 scale/speed；不改游戏 INI。
- 构建仅 NPCMounts.dll；冻结部署计划 build/ride-m/plan.json，完整备份 NPCMounts-ini-close-20261017-m。
- 步态 Motion.h、DoM 撤回状态、ESP、INI、动作资源及目录名均保持。没有新增碰撞或移动钩子。
- 游戏内未验证。保存崩溃、移动抖动和独立跳跃不能标记为已修复。本轮不能视为整个方案完成。
