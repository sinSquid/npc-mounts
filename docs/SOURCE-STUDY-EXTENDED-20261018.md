# 骑乘相关源码学习：独立实现与核心机制

日期：2026-10-18。范围：阅读、对照、记录；不修改运行实现，不构建、不部署。

用户要求：至少参考 5 个完全不同的源码库。原版/修复版不重复计数，依赖框架/调用示例不重复计数；辅助动画项目不冒充骑乘项目。学习依据是下面固定提交中的实际代码，不是项目介绍、下载量或“编译通过”。

本记录和 `SOURCE-STUDY-20261018.txt` 配套。后者保存前三类承载实现的详细阅读位置和当前架构差异，本记录补齐独立项目、关键调用过程、限制和不可照搬之处。所有“源码事实”仅针对固定快照；推论不代表游戏根因已经证实。

## 1. 独立来源索引

| 独立项目 | 固定提交 | 研究的核心问题 |
| --- | --- | --- |
| [Piggyback](https://github.com/Kroosstiich/piggyback) | e58d1b0b4819d30459af6722eee94c4d0f31b8b3 | 通用承载、乘客控制器约束、节点座位 |
| [RSE](https://github.com/Hunk92/RSE-Shoulder-Or-Saddle) | f29299b68f89dcd669d6180b5303e24585b28b68 | 人物肩扛/马背绑定与碰撞、位移拦截 |
| [Ride Along](https://github.com/Theslushbeast/RideAlong) | 7b8ec1a55d6f378b44c637117645ab0f59b1def5 | 马背乘客、玩家作为乘客、座位更新及退出 |
| [Horse Behavior Improvements](https://github.com/Monitor221hz/Skyrim-Horse-Behavior-Improvements) | 831d1e3ebc1ce6625a65d373bc47acc313d10da8 | 骑手/载体姿态分工、局部方向与地面法线 |
| [Horseback](https://github.com/usernameak/Skyrim_Horseback_Mod) | c53197fb0fe66a55df4a10397868c9ce78bd2625 | 骑乘跨空间的状态维护、原生上骑调用 |
| [HorsePower](https://github.com/Tsptds/skyrim-horsepower-SKSE) | 39993166bf4ec22811103a987379bd2d3969a1a6 | 动画请求/通知分层、跳跃、相机空间输入、菜单配置 |
| [NPC Mounted Combat](https://github.com/SamuelJohnBrown/NPC-mounted-combat-framework-Skyrim) | fbb5417ccf0d0446ba5839000281ef9a8b243984 | 原生骑乘建立、骑乘确认、受控下骑和状态清理 |
| [FestiveFlight](https://github.com/Verteiron/FestiveFlight) | 7655eeb1b5761dde6fe527c6aa118b36b7a0428d | 非普通马外观的可控坐骑、动画节拍驱动平台与飞行 |

共 8 个独立来源项目，不是宣称 8 套完全无共同思想的底层架构，也不是 8 个仓库的全量安全审计。Ride Along 的移动钩子仍参考了 RSE，借鉴关系不隐去；HorsePower 等增强真实骑乘，不等于独立实现了人形载体绑定。

以下只作同源对照，不增加独立计数：

- RSE-Fix：4294a8644dd693ca58f3b8d46095e18724a28398，RSE 派生。
- Carry Them：39199d50a5e4e3fdba843073596a82ffcfafe94e，Piggyback 的 Papyrus 消费端。
- Horse Behaviour Improvements（旧拼写仓库）：0932419afe30f5819c02a512ae5608910d8ecb31，同作者旧实现；来源 https://github.com/Monitor221hz/Skyrim-Horse-Behaviour-Improvements 。

本地快照：除 Piggyback 位于 `D:\Codex\work\npc-mounts-ref-20261013\piggyback`，其余位于 `E:\code-lib\me\npc-mounts\build\reference-l\`，依次为 `RSE`、`RideAlong`、`HorseBehavior`、`Horseback`、`HorsePower`、`NPCMountedCombat`、`FestiveFlight`。同源对照目录为 `RSE-Fix`、`Carry-Them`、`HorseBehaviourOld`。

## 2. 前三类承载实现：必须记住的核心

### 2.1 Piggyback

阅读位置：`src/hook.cpp`，`src/Piggyback.cpp` 的 Attach:469、Detach:542、UpdateAll:656、控制器/碰撞处理:276—439、缩放/目标点:441—466；详见配套初次记录。

核心链：原玩家 Update 返回 → 查载体当前节点 → 节点世界原点加按载体朝向旋转的偏移 → 更新乘客实际引用位置/角度 → 约束乘客控制器的自主运动和坠落累计。

重要实现选择：座位偏移不直接沿倾斜脊椎骨轴旋转；缩放测量缓存，仅尺寸变化时重测。Detach 恢复其修改的碰撞状态。插件的运行期绑定没有自己的序列化，Carry Them 在读档时重新建立。

能学：真实乘客的位置、碰撞、控制器是一组职责；缩放与动画起伏不要混为一项。

不能照搬：当前玩家仍承担物理移动，不能把乘客清零速度的代码套到玩家身上。挂在玩家更新后，也不能证明 NPC 载体的节点已是本帧最后结果。Carry Them 要求的 Piggyback >=1.2.2 API 不存在于本地 1.2.1 快照。

### 2.2 RSE / RSE-Fix

阅读位置：`RSE Player/src/main_v7.1_RSE-P.cpp` 的 UpdateActorPosition:60、UpdateHook:128、StartAttachmentPlayer:155、StopAttachmentPlayer:221；配套 ActorCollisionManager 的位移/碰撞钩子。修复分支相应位置详见初次记录。

核心链：原演员更新 → 节点世界位置加朝向偏移 → SetPosition(false) 与 Update3DPosition(false) → 被附着对象的自主位移另外受限。

重要实现选择：更新的是实际引用，不是只挪显示骨盆。修复分支改用 ActorHandle/节点引用并在 3D 更换后重新找节点；解除绑定与物理恢复配套。

能学：仅限制正在被承载的那个对象；结束绑定时先撤销状态再恢复资源，避免已结束对象继续被钩子处理。

不能照搬：原版裸指针和历史碰撞接口不是无缺陷范本；修复分支的整套 SEH/锁/Havok 重建不是当前需求必须增加的功能。SetPosition(false) 与 Piggyback 的 true 各有其组织前提，不可任意拼接。

### 2.3 Ride Along

阅读位置：`src/main.cpp` 的 CopyPose:544—578、Place:790、Board:870、PlaceTrip:1292、StartTrip:1318、EndTrip:1399、TickTrip:1466、Tick:1723、CameraHook:1854、MovementHook:1875。

核心链：真实马负责移动 → 分离乘客控制器 → 原相机更新后用实际鞍骨放置乘客 → 仅抑制乘客自主位移。玩家作为旅行乘客时，移动/战斗/潜行/跳跃等控制被关闭，载体由 Travel package 驱动。

重要实现选择：进入设置 OAR 标志及 HorseEnterInstant；骨骼按名称选择，排除 Root/Camera 等；退出包含姿态结束、归还控制、移到地面和物理恢复，不是一条解绑函数。

能学：骑手和载体职责清楚，Enter/持续/Exit 是完整生命周期。

不能照搬：AI 马带玩家旅行不等于 W/S 手动驱动人形 NPC，更不证明组合跳跃已解决。它的相机后更新也不等于当前行为图 Generate 时点。

## 3. Horse Behavior Improvements：方向计算与姿态分层

实际阅读：`HorseBehavior/src/hook.cpp` 全部、`src/hook.h` 全部、`src/settings.h` 全部；与旧仓库 `src/hook.cpp` 的方向算法对照。

关键入口：

1. `hook.cpp:7 Update`、`:18 UpdatePlayer`：通过 GetMount 找到真正载体，调用 UpdateLeaning，再调用原演员更新。
2. `:32 UpdateLeaning`：读取载体控制器的 surfaceNormal 与水平 forwardVec；行进向量太小时退到眼睛朝向。
3. 将法线分别投影到行进方向与其横向，计算 pitch/roll；载体静止时目标倾斜归零。
4. 对骑手脊椎/头部与马身脊椎/头部使用独立倍率、上限及插值速度。
5. 写入 HSRD_*、HSPS_* 图变量，由对应行为/动画消费。这里没有通过反复移动玩家引用制造倾斜。

旧版对 eyeDir.y 的正负翻转角度，新版改为方向投影。此差异提示：同一地面法线随朝向的解释必须在明确方向基底里完成，不能简单按世界某轴符号判断前后坡。

适用边界：这里只研究机制；用户此前要求移除当前项目的坡道判断，本轮不恢复坡道功能。仓库主体是 C++/INI，没有给出其图变量消费端的完整行为资源，不能把设置变量成功当成视觉动作成功。

不宜照搬的细节：插值值是静态共享成员，被多个骑手更新可能相互影响；UpdateLeaning 还写入 maxSlopeCosine，这不是纯视觉处理。这里是从代码结构发现的适用风险，不声称已在其游戏环境复现。

许可：新版仓库 GPL-3.0，另有 EXCEPTIONS；旧版未查到仓库级许可。没有复制运行代码。

## 4. Horseback：传送是状态迁移，不只是坐标变化

实际阅读：`src/HorseInCellsModule.cpp` 全部、对应 hpp、Module 的启用逻辑、默认 TOML；`RoadStaminaModule.cpp:94—163`。

核心入口 `HorseInCellsModule.cpp:84 ReattachMount`，调用过程：

1. 从玩家 GetMount 取得真实坐骑；通过两个原生函数重整骑乘状态（源码本身对其中含义有疑问，不能把注释猜测升级为已核实 ABI）。
2. 移动坐骑位置，并调用 MoveRefToNewSpace 更新 interior/worldspace；特定外部空间路径另处理 parentCell。
3. EvaluatePackage、StopMovement、StopCombat、EndInterruptPackage 与 AI 处理时间重整。
4. 暂时撤下基底的 Respawn 标志，避免换空间后回出生点；设置线程局部标记调用 Actor_Mount。
5. `:142 PlayerCharacter_UpdateCellTransitions_Hook` 在原过渡前执行上述处理，原过渡返回后归还 Respawn 标志。
6. `:127 Character_ValidateInteraction_Hook` 只在该线程局部重挂窗口放行验证；其他请求走原函数。

另外，两个特定调用点把 IsOnMount 查询替换为 false，用于准星/门激活。该路径是有意绕过原生限制，不是可以无条件借用的“兼容修复”。

可学：跨空间需要同时考虑真实乘骑关系、父空间、AI、物理与恢复；暂改共享基底数据必须归还。只对最后记录的非固定 NPC 跟随传送，是当前项目自己的选择策略，不由该源码自动解决。

限制：作者把 HorseInCells 标为实验功能，默认关闭；README 说明 SE 未测试，代码的调用点偏移也注明特定 AE 版本匹配。不能把 SE Relocation ID 存在视为 1.5.97 整段机器码兼容的证明，也不复制其中放宽校验的钩子。

辅助：RoadStamina 仅在实际骑乘载体、特定体力损耗调用上检查 preferred navmesh triangle 后改损耗倍率；不改移动位置，不提供座位/跳跃解决方案。

许可：GPL-3.0 文件头。

## 5. HorsePower：输入请求、动画通知、物理参数不是一层

实际阅读：`src/Plugin.cpp`、`Hooks.hpp`、`ButtonEventListener.hpp`、`Fixes.hpp`、`ModSettings.h`、`include/ModConfigMenu.hpp`；依赖 portfile。

### 5.1 两个不同入口

- `Hooks.hpp:128 OnCharacter`、`:178 OnPlayer` 拦的是发给图的 NotifyAnimationGraph 请求，随后调用原函数；上骑/下骑请求成功才注册/移除输入监听。
- `Hooks.hpp:26 AnimationEventHook::Hook` 拿到的是动画图产生的通知。它仍调用原通知处理，不能用“吞掉返回值”倒推已经发生的动作被撤销。
- `Plugin.cpp` 在预读档移除监听；读档后只有玩家仍在真实骑乘时重新注册。

可学：区分请求和结果，不要用一个事件监听的存在来证明拦截到真正起跳入口。安装 hook 成功、请求被接受、动作已播放和视觉最终正确，是四件不同事情。

### 5.2 跳跃与落地

- `Hooks.hpp:91` 在真实马的 jumpBegin 通知中调用跳跃参数处理。
- `Fixes.hpp:75/85` 读取 _HORSE_IncreasedJump，根据站立/移动选择控制器 jumpHeight；源码说明其观察到事件在默认 jumpHeight 设置后发生。该时序是其图和环境的依据，不自动适用于当前人形图。
- `_StillFalling` 分支向下射线，命中后请求 LandStart；说明落地处理也在其马图事件链内，而非靠键盘输入包办。
- HorseEnter/HorseExit、MountedSwimStart、GetUpEnd 等配合真实马/骑手行为处理。

不能照搬：当前 NPC 保持人类种族，不会凭空具备 IsHorse 分支和 _HORSE_* 图变量对应的行为；C++ 仓库未包含所需的完整图资源。马 jumpHeight 的固定数值不是本项目跳跃倍率。

### 5.3 相机与转向

`ButtonEventListener.hpp:86—158`：载体 forward 投影到 cameraRight/cameraForward，和 moveInputVec 做点积/叉积，判断方向差；低速请求特定 90/180 度 canned-turn 动画，松开或转向接近目标时 early-exit。原生移动仍存在，监听最后返回 kContinue。

可学：用同一坐标基底比较输入与载体方向。

不能照搬：这不是屏蔽 A/D 横移并改成连续转向的实现，也不解决当前 S 后退。代码两处失败分支只有 `kContinue;` 表达式而缺 return，继续后可能解引用空对象；作为参考也必须审查，不能按名气全盘复制。

### 5.4 菜单保存

`ModSettings.h` 使用 REX SettingStore，默认/自定义 INI 分开；`ModConfigMenu.hpp` 先取本地 bool，经 UI 修改后 SetValue，再调用 Save，必要时仅更新相应处理函数。

能学：区分“修改配置值”“落盘”“重新配置处理函数”；保存不必无条件遍历所有角色并重装动画。

限制：它使用自身附带的 SKSE Menu Framework 包装头。不能凭相同 UI 函数名就证明本地当前框架版本 ABI、回调和参数签名匹配，更不能把它的保存方式当成本项目既往崩溃的已证根因。

许可：AGPL-3.0。公开学习与直接复制/发布不是同一授权范围。

## 6. NPC Mounted Combat：接受请求不等于已经骑上

该仓库是 VR / 旧 SKSE 体系，含硬编码 VR 地址；只学习上骑状态链，绝不复制地址、函数序言拆解或原生接口到 SE 1.5.97。

实际阅读：`Helper.cpp:274—546 DismountHook` 和 `:687—834 SetupDismountHook`；`HorseMountScanner.cpp:981—1144 AttemptMountHorse`、`:1150—1282 CheckAndTriggerMounting`、`:1435—1491` 初始化/读档清理；`MountedCombat.cpp:671—772` 重挂扫描。不是整个战斗框架审计。

### 6.1 建立真实乘骑

`AttemptMountHorse:1000`：确认本次不在重试/布娃娃/已有乘客等状态 → 记录尝试状态 → 临时停止战斗警报并收武器 → 把 NPC 放到马旁 → 原生 ActivateRef(horse,npc) → 根据返回值设置“已接受激活、等待真正乘上”或清除目标。

`CheckAndTriggerMounting` 不把 activationSucceeded 当 remountedSuccessfully；等待期间不再重复发激活请求。源码同时存在持续位置纠正/冷却流程，不能只借用其 ActivateRef 一行。

可学：上骑动作开始、原生关系建立、位置跟随稳定应分阶段确认；每帧重发进入姿态不是天然正确做法。

### 6.2 下骑与恢复

`DismountHook` 优先放行玩家、非目标、离开适用空间、死亡、被抓取、退出战斗等情况；只在真实已骑乘 NPC 的特定战斗路径阻止下骑，并清理其追踪。读档 ResetHorseMountScanner 清旧目标/计时/扫描状态，不把前次运行的裸状态继续当有效。

可以借鉴职责范围，但不能引入当前已取消的敌对目标法术或 DoM 兼容改动。该源码默认武器、战斗跟踪等是它自己的功能，与当前需求无关。

不照搬：安装器自己识别有限指令模式；识别不足时假定固定长度。未知函数序言不能靠固定字节数兜底，这种写法不能解决本项目此前的 hook 兼容问题。并且已读 DismountHook 路径未用 PreventNPCDismountOnAttack 开关决定放行，不能因为有同名配置/日志就认定设置实际生效。

许可：此快照未找到仓库级 LICENSE/COPYING，不把公开可读当任意复制许可。

## 7. FestiveFlight：保留原生承载，改变载体的环境

实际阅读 `dist/Data/Scripts/Source/` 内：`vRDN_FlightMEScript.psc` 全部，`vRDN_RudolphActorScript.psc` 全部，FlightTestActi、RudolphPlatformActi、EmergencyLandingActi、MetaPlayerAlias、MetaQuest。

### 7.1 载体和飞行

NPC Actor 脚本仍使用 IsBeingRidden 判断原生骑乘；飞行效果没有每帧单独把玩家人形身体搬到另一角色的骨盆。

`FlightMEScript:95` 创建三个可循环复用的平台引用，注册坐骑 syncRight/syncLeft 动画通知：

- syncRight → CheckFlight，施放检测法术，用探测到的 hazard 类型判断飞行/着地。
- syncLeft → UpdatePlatforms，从玩家视角取俯仰/朝向，限制俯仰并平滑；禁用下一平台、搬到载体脚下、设倾斜、启用，循环切换三个平台。
- 因而这是“通过脚下平台改变载体可走的表面”的路线，不是通用角色附着或直接控制悬空人形 NPC 的证明。

### 7.2 收尾与重新载入

EffectFinish → CleanupPlatforms 删除三个引用；平台 CellDetach/定时回调也删除临时对象；MetaPlayerAlias.OnPlayerLoadGame → DoUpkeep → 更新别名/载体配置；EmergencyLanding 使用 TranslateTo 载体，不把玩家与载体分别插值两条路径。

可学：特殊运动机制必须给临时资源、落地与读档指定负责人；稳定更新节拍可以来自载体动画，但这要求确实有这些动画通知。

限制：它改变碰撞环境、依赖配套资源，可能影响导航/碰撞，不能为本项目新增平台当成小修。Jump 控制注册在这份脚本里被注释，即使存在 OnControlDown 函数，也不能写成实际已接管 Space。脚本检查飞行的全局值也不是控制器真实落地状态的同义词。

许可：此快照未找到仓库级许可证。未复制代码或游戏资源。

## 8. 额外辅助源码：不计入 8 个核心来源

### 8.1 OpenAnimationReplacer：替换片段有生命周期与混合

来源 https://github.com/ersh1/OpenAnimationReplacer ，提交 f4e7688b065175aff70aa523073857911e15aca3；本地 `build/reference-l/OpenAnimationReplacer`；GPL-3.0（COPYING）。

实际阅读 `src/Hooks.cpp:1—230`；`src/ActiveClip.cpp:89—260`、`:276—550` 的替换/更新/激活/退出/输出处理，不是整个 OAR 审计。

核心：Activate 前做条件选择，改 animationBindingIndex 才真正换片段；保留原 index/mode/flags/triggers 用于恢复。中途切换根据配置在 interrupt/loop/echo 时重新判断，确有改变时排队；可保留局部播放时间，旧片段保留作过渡混合。新动画还未加载时用旧片段输出，避免一帧 reference pose。

`ActiveClip::OnGenerate:508` 对 TRACK_POSE 做过渡混合；这是 clip 输出，不自动等于整个角色最后提交的骨骼姿态。Hooks 中还存在 behavior graph Generate 的后处理供预览使用，二者不是一个层级。

能学：条件成立、片段被选中、片段激活、混合完成、最终显示必须区分；避免无条件重复启动同一姿态。不能因为发送事件少了，就断言没有 blend/IK/别的写入。

不能据此断言：当前左右晃动就是 OAR 的错；也不能凭回调执行日志宣称最终人类跳跃已被消除。本轮没有改 OAR 或当前步态。

### 8.2 Skyrim's Paraglider：物理运动与专用姿态一起改变

来源 https://github.com/LXIV-CXXVIII/Skyrims-Paraglider ，提交 f7d0d30f9f50da679706ab933f887fdcf1b868af；本地 `build/reference-l/Paraglider`；MIT。

实际阅读 `src/main.cpp` 全部 276 行。核心在 `Paraglider`：先走原更新 → 根据激活/图变量请求 StartPara/EndPara → 在对应路径修改控制器竖直速度 → 落地清理激活状态；退出还处理 fallStartHeight/fallTime。

能学：物理空中状态与视觉姿态要配套设计，改变速度并不会自动得到目标动作。

不照搬：此旧实现也会重复发送 Start/End，进度按固定更新量增长、全局状态多、接口/路径特定，不能当成本项目反复发送姿态的正当性或跨帧率稳定性的证明。它不是骑乘，不计核心项目数。

### 8.3 Chair Horse：外观替换不是人形骑乘绑定

来源 https://github.com/markekraus/ChairHorseSkyrimSEMod ，提交 36309fba9369b4afd36540cb35abc2deab5e4a4b；本地 `build/reference-l/ChairHorse`；MIT。

阅读 `Source/Scripts/` 全部五个 PSC。购买片段设置马阵营/所有者及 PlayersHorse 别名；换外观在 Outfit 列表轮转；召唤从四个方向尝试 MoveTo，用 LOS 选择位置。

能学：拥有权、外观、召唤可以分离管理；同一个坐骑不必因外观变化重建整套运行状态。

限制：已读脚本没有提供人形玩家骑人形 NPC 的绑定/移动/跳跃实现；没有进一步解析其 ESP/NIF，所以不声称从这些脚本已验证模型重绑定或骨架兼容。LOS 也不等价于地面、碰撞和空间全部安全。只作辅助。

## 9. 落到当前项目的对照结论（仍不改代码）

| 当前问题/需求 | 已读源码给出的依据 | 不能越过的边界 |
| --- | --- | --- |
| W 前进时身体左右晃动 | Piggyback/RSE 更新实际乘客；Ride Along 区分载体与乘客；OAR 有独立 clip 激活/混合 | 不能凭外观就判定实际在播上下马片段；不能只来回切 Root 保留开关 |
| Space 仍显示人类跳跃 | HorsePower 区分请求和通知并依赖专用图；Paraglider 物理与专用姿态配套 | 输入拦截成功不等于图状态/最终姿态被约束；不能直接发送马事件给人类图 |
| S 后退回归/横向运动 | HorsePower 同一相机基底解释输入；真正载体方向与乘客位置职责不同 | canned-turn 不是本项目后退实现；不复制死区、速度常数冒充证明 |
| 座位贴合和 scale | Piggyback 节点原点/朝向偏移/缓存尺度；Ride Along 的实际 saddle 节点 | 不把两套物理模型、不同帧阶段的坐标任意混合；平滑不能替代时序正确 |
| B、下骑、读档完整还原 | RSE 撤销绑定后恢复物理；Ride Along 配套恢复控制；多项目明确重建/清旧运行态 | 不加入无关 DoM、战斗系统，不恢复用户已取消的上下骑动画 |
| INI 与中文菜单 | HorsePower 配置存储与处理函数更新分开 | 外部包装头版本不等于本地 ABI；只能参考保存职责，不能照抄签名 |

目前仍缺的证明：没有一个已读项目直接证实“玩家普通人形控制器留地面驱动，NPC 跟随玩家，玩家显示骨盆再移到 NPC 背上”这一混合结构的最终动画/移动所有权。增加参考数量不会填平这项差异，必须明确承认。

学习后的约束：先确定谁唯一负责位移、谁负责可见姿态、在哪个有效时点绑定；同一事项不得同时依赖两种相反的角色前提。对能确认的局部机制做最小迁移，不把完整他人框架搬进来，不把更多空引用/兜底分支当作根因修复。

## 10. 本轮变更与验证边界

- 仅新增/更新研究文档、保存独立参考源码快照；没有改运行代码、测试、INI、ESP、动画、DLL，没有构建部署。
- 没有把本研究登记成“Mod 已优化”；没有新增整包回退备份，没有提交/推送。
- 阅读代码证明其实现方式，不证明它无 Bug，也不证明本机游戏兼容。当前用户报告的 W 晃动、人类跳跃、S 回归仍为未解决。
- 新下载参考采用仅本次 git 命令禁用失效代理，未修改全局代理配置。参考源码不安装到游戏，不新增 Mod 前置。
