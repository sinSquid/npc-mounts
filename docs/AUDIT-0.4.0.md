# 0.4.0 — 2026-10-16

候选修正版；SE 1.5.97。完成源码、Release构建和离线回归，不等于游戏动画/碰撞/SMP验收。

## 证据与改动

1. 现有0.3.5日志中0001A6A4在转换、上骑后约1秒自动下骑/还原。代码将任意GetCurrentScene当作拒绝/退出条件，范围过宽；删除这一条件，保留击杀、死亡、加载等实际危险状态检查。家具目标先退出交互。拒绝转换时显示并记录具体原因，不以种族白名单筛除自定义人形。
2. 只读解析MoolManDu.esp：5个NPC使用3个自定义Nord派生种族，行为路径为Actors/Character/DefaultFemale.hkx；6个独立NIF都含Root/Pelvis/Spine2。没有修改该Mod或将种族替换为马；这不证明运行时图绑定已成功。
3. 新Crawl.nmc从已验证的本地99骨人形爬行片段生成，7组idle/walk/run/sprint/back/left/right。在hkbBehaviorGraph生成输出后采样并写入对应人形姿态，持续推进相位，不仅靠restrained情况下可能不推进的移动事件。只按名称/父级匹配99轨，允许Look辅助骨别名及额外骨骼；不匹配拒绝，不盲写。首个成功输出记Native crawl output active。重绑失败限每秒再试，B/删除/读档清理绑定。
4. 新上骑过渡反向播放已有31帧下骑资源，默认0.85秒；暂停水平输入/速度，末帧输出确认后释放，异常额外等待最多0.25秒。B在过渡期间排队，下骑后还原；RIDE记录包含上骑期间原锁定标志，读档不遗留锁定。
5. 人魅-束缚敌对目标先检查敌对、依赖和可生成固定桩，再执行坐骑化、固定、固定桩绳及敌对抑制。保存束缚前的“已转换”状态，F只解除固定，B先释放绳/束缚再恢复转换前状态。失败生成桩不转换，转换失败删除桩。异步绳失败撤回固定但保留已成功的坐骑化。旧post=0的D存档仍按旧束缚清理，不凭空生成新桩；解除后重施采用新行为。
6. C/D保留AI及强制动画更新，让原生姿态输出继续工作；MovementBlocked/restrained/速度清零约束自主移动，临时清除队友标志。外部强制传送仍不拦截、不回拉，移出范围后解除固定。固定桩FormID读档无法解析时标记待释放，避免遗留限制。
7. INI/中文菜单新增MountAnimationSeconds（0.4—2，默认0.85）、JumpHeightMultiplier（0.8—2，按0.1量化，默认1）。起跳仅一次按sqrt(系数)调整玩家控制器向上速度，不改全局跳跃设置、不逐帧累乘；空中rank=2播放人形爬行奔跑。下骑/B/读档仍清理自身状态。

原6996 HKX、NPC种族、美化、模型、OAR配置保持。新动画输出路径针对无动作/姿态抖动；没有运行证据可宣称全部抖动或拖影消失。

## 验证

- 两轮20组实际C++函数提取/测试替身回归通过。新增真实Crawl.nmc采样/99及126骨映射/父级错误拒绝/额外骨骼保护/清理；上骑30/60/144FPS、末帧确认、B排队、超时、原标志；一次跳跃冲量；敌对捕获组合及失败回退；INI量化往返。既有恢复/牵引/加载/输入/座位/方向交接测试通过。
- 第二轮在最终代码上运行；首轮后仅新增每次绑定首次输出的诊断日志。日志：build/audit-0.4.0-final-round1.log及final-round2.log。
- Release构建、SKSEPlugin_Load/Query/Version导出检查、git diff --check通过。
- 包检查：22独立ESP记录、6法术书、只有Skyrim.esm主文件、无NPC/RACE覆盖、18个验证HKX身份对应6996路径、NMP及新增NMC结构通过。
- DLL SHA-256：B3E749A92A0E5F2A55E219334F5F934B5CCDECCBB66C5E29DE249063A87C8834。

## 部署

目标：E:\iniRePather-Skyrim\Mod Organizer 2\mods\【实验室】NPC坐骑-ai编码。

完整旧版备份：E:\iniRePather-Skyrim\Mod Organizer 2\backups\NPCMounts-0.4.0-20261016；_evidence保留原改动记录、modlist和日志。

冻结计划：reports/audit-0.4.0-plan.json。仅替换DLL、ESP和INI，新增NPCMounts/Crawl.nmc，最终7015文件。INI原有值逐项保留，包括AllowBackward=1、NPCScale=1.40999997、SpeedMultiplier=1.60000002；只加两项默认参数。目录名、MO2 profile、其他Mod不改。无提交推送。

## 游戏复测边界

重启后确认Ready 0.4.0；对已有转换目标可先完整还原后重施。测试饺子目标转换/爬行、上骑/移动/转向、跳跃0.8/1/2、上骑途中B、固定后传送、D→F与D→B、保存读档。查看Native crawl output active是否实际出现。不能用离线替身证明真实引擎行为、第三方动画钩子次序或SMP视觉效果。
