# 0.3.5 — 2026-10-16

状态：候选修复；离线验证不能替代游戏实测。目标目录保持【实验室】NPC坐骑-ai编码。

## 实际改动

1. 六个魔法/书/效果保留现有FormID及EditorID，修正ESP说明、道具说明、HUD及配置界面的字母简称。
2. 对已转换目标重施转换时，不再直接无操作返回；恢复派系/动作、重新应用缩放，保留原始还原账本。还原中、死亡、未加载时不抢占；骑乘/固定/牵引中不重复重置动作。自定义目标的交互不再依赖原生对话Activate的接受结果。
3. 固定桩使用当前安装LeashFramework的ApplyStandaloneLeashAtPosition，背骨至桩上方48单位。已有本插件玩家绳先断开再生成固定桩绳；第三方绳拒绝覆盖。没有框架则拒绝固定。创建失败、检查超时、外部断绳后撤回固定；异步解除期间保留清理请求。
4. 固定绳不被标记成跟随绳，DisconnectLeash的holder为None。TETH v2保存anchor标志，读取v1默认玩家绳；BIND仍为v2。读档核查固定绳，旧固定状态在加载时尝试补绳；不阻拦外部传送，不跨cell强行拉回。
5. 玩家牵引明确指定NPC跟随，禁用该条绳的布娃娃与持绳人过伸阻挡。NPC更新前释放restrained/MovementBlocked及bAnimationDriven/bAllowRotation交接门，保持原人形爬行动作与种族。
6. 允许后退时优先放行映射到Alt的Back。骑乘移动交给原生玩家控制，不允许动画图根运动同时接管。接入TDM公开V1接口，仅骑乘期间申请禁用方向接管；普通下马、紧急下马及读档清理自身请求，不改TDM文件。

## 两轮检查

首轮：16组实际函数提取的C++回归通过；覆盖输入、还原、缩放/速度账本、座位、加载中断、异步绳索和TDM所有权。新增固定绳创建/失败与TETH v1/v2迁移检查；发现并修正异步解除固定后没有继续清理本地桩的路径。

第二轮：17组C++回归全部通过，日志build/audit-0.3.5-round2.log。增加复活重施1000次的账本保留测试、原生Activate拒绝时的自定义入口、Alt映射后退、固定绳/玩家绳分流、两类绳清理及外部中断检查。玩家牵引改接桩在后续单项回归也通过。Release构建、SKSE三项导出与独立完整候选包检查通过（22条记录、6本法术书、18个独立动画、6996个HKX路径）；名称/说明不再含字母法术简称。

DLL SHA-256：1A0F10CE98239236FC8C8AEB0BBAC19D9E6F6BEDEDA4C0B63B364F9E30FE8891。部署计划reports/audit-0.3.5-plan.json，只含NPCMounts.dll和NPCMounts.esp；完整备份位于MO2/backups/NPCMounts-0.3.5-20261016，另保存原改动记录、modlist及日志。

部署完成：最终7014文件全量哈希验证通过，改变2文件、保留7012文件；再次包检查通过。原记录第84项更新至第80点，UTF-8解码通过，其他Mod条目保持；modlist备份与当前SHA-256一致。用户INI保留AllowBackward=0，测试后退需在菜单开启并保存，未擅自覆盖配置。

参考接口：https://github.com/ersh1/TrueDirectionalMovement/blob/master/src/TrueDirectionalMovementAPI.h

限制：没有进入游戏重现，不能声明已消除所有镜头抖动或已验证NPC实际跟随；其他输入/动画/相机Mod仍需实测组合。没有更改美化、NPC种族、6996条HKX资源、用户INI或其他Mod。未自动提交/推送。
