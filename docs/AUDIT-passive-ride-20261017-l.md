# 2026-10-17 L：被动坐骑位移、相机分离及菜单诊断

## 状态及证据边界

这是骑乘修复候选和保存崩溃诊断，不是全部问题已解决的声明。游戏当前未运行，未进行游戏验收。

用户报告 K 版点击保存立即崩溃、骑乘移动异常及独立跳跃。最新可用崩溃文件仍是 00:54 的旧碰撞故障；不能拿它解释保存崩溃。

## 本轮相对 K 的生产修改

1. Config.cpp：原保存逻辑不改，增加保存开始（线程 ID）、文件写入/关闭结果、配置及结果文本发布、当前帧绘制完成四段立即刷盘日志。DebugLog=0 也记录；只在点击保存时产生，不增加每帧日志。
2. RideDrive.h：删除持续设置/恢复 fovSlideMode。该字段是静止观察模式，不能作为骑乘自由观察保证。A/D 转向、W/S 输入和步态不改。
3. RideHooks.h：核对本机 SE 1.5.97 后，骑乘期间采用第三人称自由旋转接口；非骑乘继续前置原处理。仅在当前被骑乘 NPC 的原更新栈内抑制其自身移动步骤，玩家和其他角色继续原生移动。
4. plugin.cpp：NPC 原更新前仍只同步一次；在原更新调用前设置线程局部被动角色，返回后恢复。没有新世界坐标追赶，没有新的碰撞钩子。新入口校验不符时不开放骑乘，不使用未核实地址。
5. 跳跃仍由玩家物理控制器产生一次整体运动；本次消除被绑定 NPC 继续独立应用位移的竞争。没有另写跳跃冲量算法，也不能据此宣称游戏内玩家独立跳跃已验收。

保留 Motion.h、Seat.cpp、Crawl.cpp 的 K 版行为；保留完整六魔法、牵引/固定/最近坐骑逻辑、原生交互入口及 DoM 撤回状态。仓库相对 main 的旧 J/K 差异仍存在，不属于本轮全部新增修改。

## 接口依据

- Ride Along 7b8ec1a 与 RSE 修复分支 4294a86：被承载角色不同时接受自身移动和座位绑定。
- TDM 57b913a 的 ThirdPersonStateHook：第三人称自由旋转接口；它是相机辅助依据，不计入五个承载项目数量。
- tools/inspect-ride-entrypoints.cjs 从实际 Address Library 解析 36359、256647，并核对实际 SkyrimSE.exe。
- 位移调用 RVA 5D8260 → 5D87F0：调用者 RCX=Actor，XMM1=delta，被调用者保存同一参数，返回值未使用。
- 相机表 RVA 162F2A0，槽 0xD → 84FA20：RCX=ThirdPersonState，DL=bool；头文件 freeRotationEnabled 位于 DC，与机器码一致。
- 仅支持已声明的 SE 1.5.97，不将其他运行时的源码偏移照抄部署。

## 测试方法

- tests/menu-runtime.cpp + prepare-menu-runtime.cjs：保留生产 Config.cpp 的 Render/Read/Write/Set，只替换预编译头依赖；加载实际安装的 SKSEMenuFramework.dll（ImGui 1.90.8），建立真实 CPU ImGui 帧，用鼠标事件点击实际按钮/复选框，不伪造按钮返回值。
- 使用隔离 build/menu-runtime/Data 路径，不接触游戏 INI。每轮 50 次诊断开关切换、50 次保存、50 次重新读取，检查 scale=1.9、speed=1.6 保留。
- 此测试不包含游戏渲染线程、其他插件与 MO2 虚拟文件系统，无法排除游戏内崩溃。
- 两轮原生回归检查生产函数；新增被动角色精确匹配、其他角色/玩家原调用、线程局部隔离、自由相机控制归还。它们不是引擎运动模拟验收。
- 保留实际 Crawl Install 文件入口检查、旧碰撞 ABI 机器码与 MASM 桥检查；没有通过放宽资源校验取得通过。

日志与冻结计划：build/ride-l。独立参考仓库在 build/reference-l（不加入发布包）。

## 部署约定

目标：【实验室】NPC坐骑-ai编码，保留目录名。只替换 NPCMounts.dll；原 INI、ESP、动作及其他 7023 个文件保持。

部署前完整备份：E:\iniRePather-Skyrim\Mod Organizer 2\backups\NPCMounts-passive-ride-20261017-l；_evidence 保存原记录、modlist、日志、计划和验证结果。

候选 DLL SHA-256：62DE091E9C4997DF6B00301546AC0D9C8AFF3D2B0BEAC8E1920D216293C17DB1。

已完成：两轮各 27 组 C++ 离线回归、入口机器码检查、独立 MASM 碰撞 ABI、两轮真实菜单 DLL 独立进程测试和包检查；完整备份已验证。已部署并核验全量 7024 文件，仅 DLL 改变；游戏验收为 false。全局记录已在原 NPCMounts 条目补充 140—144 项，其他 Mod 内容和 modlist 保持。未提交推送。

后续游戏验收仍包括：点击保存；Serana/五人组上骑；前进、后退、A/D、鼠标；坡道/跳跃；下骑和 B；固定/牵引/传送及读档。保存崩溃若再现，应先读取四段日志与对应新崩溃栈，不再用独立测试通过代替根因。
