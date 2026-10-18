# N：前置钩子安装兼容（2026-10-17）

## 确认的问题

12:21:13 游戏日志确认 M/L 的 RideHooks 安装检查拒绝了入口，导致无法乘骑。旧检查要求 E8 及四字节相对位移都与磁盘原版一致；但正常的前置五字节 CALL 钩子会改变位移。

本轮用未经修改的旧 Install 在可执行内存中复现：原版调用可安装，先安装真实 CommonLib 前置钩子后返回失败，退出码 42（build/ride-n/old-installer.log）。这证明了兼容缺陷，但旧游戏日志没有输出入口字节，不能据此指认某个已安装插件或证明游戏中一定是同一种改写。

## 最小生产修改

仅修改 src/RideHooks.h 的 Install：

1. 已安装时不重复挂钩，避免前置指针变成自身产生递归。
2. 验证已知 SE 1.5.97 调用点仍为五字节 E8 CALL，不把目标位移当作固定版本签名。
3. 继续使用现有 CommonLib write_call<5>，返回的当前目标作为 originalMovement，保留前置跳板，不直接跳到原版地址绕过其他插件。
4. 不识别的指令不写入；错误日志输出现场五字节和地址。成功日志输出前置运动及相机目标。

没有修改 MovementDelta、FreeRotation、步态、座位、输入、跳跃或魔法逻辑，没有恢复 DoM 兼容。也没有扫描或重写其他插件。

## 依据和本地执行验证

- 本地 RideAlong/src/main.cpp 的 ApplyMovementDelta 安装仅要求 E8，并用 write_call<5> 接续目标；RSE-Fix/RSE Player/src/ActorCollisionManagerRSE-P.cpp 使用相同调用点和参数。
- 实际链接的 CommonLib Trampoline.h 的 write_branch 返回 a_src+5+有符号位移，Trampoline.cpp 写入 E8 到 FF25 跳板。因此保留返回的跳板地址即可接续，不需要自行剥离多级跳板。
- tests/prepare-hook-install.cjs 保留生产 Install 和钩子函数，仅把游戏地址解析、SKSE 全局分配重定向到隔离测试内存。
- tests/hook-install.cpp 链接生产使用的 CommonLibSSE，真实分配可执行内存、写入五字节 CALL 并执行 Win64 调用。不是用假的 write_call 返回值模拟成功。
- 验证原版、前置钩子、后置钩子及双层链；RCX/XMM1 参数；只抑制当前坐骑而保留其他角色；解除后恢复；重复安装不写入；90/E9/FF 等不支持入口不改变代码或相机表。
- 地址查找和相机虚表存储是隔离替身，未运行 Skyrim 引擎，不称为游戏内验证。
- 新增 tools/test-hook-install.ps1 并接入 test-native.ps1，避免以后完整回归又遗漏安装函数。

## 两轮复核与部署

两轮各执行安装链实测、原有 27 组生产函数回归、游戏 EXE 入口检查和独立 MASM 碰撞 ABI 检查。日志位于 build/ride-n/round1.log、round2.log。

只部署 NPCMounts.dll 至既有【实验室】NPC坐骑-ai编码；不覆盖 INI、ESP、动作，不改目录或 MO2 配置。完整备份：E:\iniRePather-Skyrim\Mod Organizer 2\backups\NPCMounts-hook-chain-20261017-n。

步态及座位修改前后哈希一致：

- Motion.h：3F546E77FED1030D56E3C050C82FFE0E7F9DB8FA7E4E27A86AF2BE6C867C70F8
- Seat.cpp：098064687FE5C2E72066018CEF82F79E74769CFB276D67A1B1D0A131B8576212
- Seat.h：2AC9EC239125AF306D7CB8D0C00C9800D930454FD80EFB2C93B6FF287435D8AE

候选 DLL SHA256：A93C03572BF7A0F11FCE1CC4A733892C8A5B317112718E571AAA8AD1D1EC3923。

本轮不要求用户反复启动游戏。已修复并在本地重现/验证 E8 前置钩子的拒绝问题；游戏内实际乘骑、移动抖动和独立跳跃未验收。
