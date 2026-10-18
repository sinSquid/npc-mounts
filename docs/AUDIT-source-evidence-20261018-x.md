# X：源码与原日志联合核查，局部候选未部署

2026-10-18。目标仍是 W 前进晃动、玩家人类跳跃、S 后退回归；三项尚未全部确认根因，不标记修复完成。

## 已复用的证据

- 完整 W 日志 `C:\Users\admin\Documents\My Games\Skyrim Special Edition\SKSE\NPCMounts-runs\20261018-022130-265-28240-55285015.log`，与 NPCMounts.log 相同，共 1162 行。
- 8 个独立参考项目的固定提交及机制见 SOURCE-STUDY-EXTENDED-20261018.md；本轮额外核实 activeragdoll 对生成输出与后置 Foot IK 的区别，但不能仅凭该机制认定本机 Foot IK 就是唯一根因。
- 当前源码、已提交 49fe71a、V/W 审计记录及实际部署的 Crawl.nmc、三份原始 HKX。没有把旧版“通过”作为当前正确的证明。

## 1. W：确认存在资源级大幅缩放和座位目标摆动

脚本 `tools/audit-ride-evidence.py` 直接解析实际部署的 Crawl.nmc，用其真实父链和变换计算背部模型坐标：

- run/sprint 的 Spine2 模型 X 从 -17.189449 到 56.325133，摆幅约 73.51 单位。
- 当前 Seat::Update 调用 Crawl::Anchor，返回这同一采样背部坐标，且仅滤局部 Z，X/Y 未滤。因此这些资源产生的横摆会直接进入座位目标。这一具体传播路径已由源码与部署资源确认，不是把所有抖动归为相机噪声。
- run 的 Root X scale 从 0.591519 到 7.589281，Y/Z 约为 1；walk 和 idle 也有非均匀缩放。原构建器及加载器仅校验正数/有限值，因而这些值会通过，现有离线测试也没有识别它们。
- W 日志 74 个前进窗口，73 个有显著水平速度，21 个骨盆相对背部水平距离超过 20，最大 44.44；本插件只有一次上骑记录，没有下骑过渡启动记录。日志不能识别用户疑似看见的具体片段，不能写成确认反复播放上下马。

### 独立核查，避免只信同一解码器

现用 PyNifly 解码器读原始 sneakrun_forward.hkx 的 Root X scale 同样为上述范围。为排除单一解码器的自证：

1. 从 PredatorCZ/HavokLib 官方发行下载 HavokPy v1.8.4 与 HavokToolset v1.10.19，隔离在 build/seat-evidence-x；Python 3.14.0 嵌入版仅用于加载该 pyd，不改系统 Python。
2. HavokPy 导出的 XML 缺少 spline 派生成员，直接 SDK 转换仅产生 256 字节空 KF，明确废弃该结果，不拿“命令返回 0”当验证。
3. `tools/export-sdk-spline-xml.py` 从原 HKX 搬运原始 49200 字节压缩流及元数据到 XML，不经过 Python 样条采样，不修复/重写压缩流；原类与骨名由 HavokPy 读取。
4. 用本地 Havok SDK hkxcmd 将该 XML 生成 WIN32 HKX，再 ExportKF：55232 字节 HKX、50730 字节 KF；SDK 明确报告 Root、COM 等 non-uniform scaling。
5. `tools/inspect-sdk-kf.py` 独立解析 SDK 的 KF 根轨道缩放键，得到 0.863840—3.196427。KF 只能表示统一缩放，这些值对应 `(Python原始X + Y + Z)/3`，例如 X=7.589281 对应 3.196427。因此异常不是 Python 一侧凭空生成的尺度。

边界：它证明原始资产本身包含这些数据，以及当前目标计算受其影响；还没有证明所有世界坐标残差都只由它造成。未把原动作 scale 强写为 1，未盲换动作包，未改变座位/步态。对这种非均匀动画，当前自算骨链与引擎最终 NiNode 的尺度处理是否完全一致仍待核实。

HavokToolset 默认 GLTF 转换会跳过部分非控制骨缩放，不能作为原始尺度没有变化的证据；MANUAL 模式本轮未完成，已停止本轮自己启动的转换进程。保留成功的 SDK 证据而不冒充此路径通过。

## 2. S：排除本轮采样中的“输入被吞/完全不移动”

12 个 S 窗口均有显著速度；把采样水平速度投到 `(sin(yaw),cos(yaw))`，均朝后，且图中 NPC rank=4。源码 RideInput 未删除允许后退时的 S，RideDrive::Before 仅清 X、不清 Y。

因此这一轮观测不支持把 S 的视觉回归当作按键过滤错误。不能据此否定用户反馈，也不能说所有场景均可正常后退；这里只定位到后退物理输入正常而展示/贴合仍异常。保留 S 原实现，不无据调整输入、转向或速度。

## 3. 跳跃：确认倍率处理有独立的时序错误

W 日志 02:24:33.851 的状态为 Jumping，vz=1.1960318；下一帧 InAir 时 native vz=4.3018003。旧 `air && pending && vz>0` 在前一阶段便触发，乘到的是已有地面/支撑上升速度，而不是后续引擎发布的跳跃冲量；提前消费 pending 后，实际冲量又可能覆盖前一次改写。

这是确定的物理倍率错误，但不是已经确认的“人类跳跃动作”完整根因。日志证明 Space 已被本插件接管并发出 4 次控制器请求，不能继续把错误归为原 JumpHandler 必然收到了它。

### 本轮唯一运行逻辑修改（源码候选）

- 在 JumpInput 构造控制器 jumpHeight 时乘配置高度倍率，让原生控制器以该高度生成运动。
- takeoff 仅在 InAir 且有 pending、正向速度时确认；不再事后乘速度，不改 wantState 接口，不插入任何动画事件。
- Motion.h、Crawl.cpp、Seat.cpp、RideDrive.h、RideHooks.h、上下骑及 DoM 撤回状态不改。

参考依据：本机已核实的 JumpHandler → actor jump → controller 写入接口；HorsePower 区分跳跃参数和动画通知职责的实现。不是复制其马图事件或固定跳高数值。

## 4. 回归与交付边界

`verify-ride-jump.cjs` 增加 0.8/1/1.2/2 倍率与多 scale 请求高度检查；`verify-mount-timing.cjs` 使用上述 W 的“Jumping 已有正速度”数据，确认不提前消费、InAir 只确认一次、没有后置速度写入。它们验证明确的函数契约，不证明最终游戏画面。

运行源码修改前的 plugin.cpp、两个测试和已部署 W DLL、原全局记录保存在 `build/seat-evidence-x/`，没有覆盖当前部署、没有创建新的整包游戏备份。

Release 候选 `build/Release/NPCMounts.dll` SHA256：DEFACE2C39F35625E982B75F83865D086FA3525BBABDC52BF83074F637F8D81E。

当前游戏 Mod 仍为 W DLL：5A3A9CA08B6177351A5DF207407666509CE9195B1B4FAD8BFA3BBF29E68BB956；INI、ESP、动作库、MO2 配置及目录不变。没有部署本候选，也没有提交/推送；不能把局部倍率修正描述成三个故障已修好。

最终完成两轮各 30 组生产函数离线回归、已安装 EXE/安装链检查和独立 MASM ABI 检查；两次进程结束码均为 0。用修改前 W 源码运行新增起跳时序检查，实际失败，退出码 -1073740791；当前源码通过。这不是游戏渲染验证，且原测试通过与本轮发现的资源缺陷可以同时成立，不能再把前者当作动作质量验收。

Seat.cpp、Motion.h、RideDrive.h、Crawl.cpp 修改前后 SHA256 相同。独立 SDK 使用的原始动作副本与 Mod 内来源文件 SHA256 相同：0A10584B0C6A3D5492738DAC3B28BBD56E3EF5D093379F4556A398338FD83162。

### 仍缺的证据，不能靠猜测补上

现有日志没有逐帧记录最终骨架、Foot IK 前后姿态、活动片段及输出插值，无法确认是谁在座位输出之后改变身体姿态。它也不包含所有 NPC/场景的后退复现。因此本轮不增加未核实的 Foot IK 钩子、不清除未知输出轨道、不放宽骨架校验、不任意消掉原生 Root、不把资产尺度取绝对值或统一写成 1 来掩盖问题。
