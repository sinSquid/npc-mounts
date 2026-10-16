# 0.4.1 — 最近坐骑传送跟随，2026-10-16

## 行为

- 成功骑乘时记住一个NPC引用；后来骑别的目标即替换。正常下骑不清除，解除坐骑化/死亡/删除后清除，不回退到更早目标。
- 新增独立TRVL v1 co-save，仅存FormID并通过ResolveFormID重映射。原NMNT/RIDE/BIND/TETH不改。旧档没有记录，升级后需再骑一次；Revert及读档清除位置观察基线，读档本身不召回。
- 监听PositionPlayerEvent::kFinish，事件只置原子标记、不操作NPC。游戏更新中等待加载菜单关闭、玩家cell附着/3D就绪及0.75秒稳定时间，再行动。过门/换世界及单次大位移（1024单位以上）另作兜底；普通走路跨室外格不判作传送。未发引擎事件的第三方短距离瞬移可能无法检测。
- 查找玩家附近落点（优先后方，8方向/两档距离，按NPC scale调整）；射线检查路径遮挡与地面。有落点才请求一次引擎MoveTo，同步指定目的cell、world与位置，不先移动再对加载中的3D补SetPosition。不做MoveTo钩子或持续拉回，不重新建立骑乘关系。
- 使用CommonLib TESObjectREFR::MoveTo_Impl同一引擎入口，Address Library 56227/56626；本包仍只支持SE 1.5.97。现有角色源区域正在卸载时等待；未加载的引用交由引擎正常加载流程处理。射线不是完整导航网格/胶囊可达性证明，狭窄区域仍需游戏验证。
- 每0.25秒检查一次待执行召回，最多10秒；无可用落点/状态未就绪则放弃本次，下一次玩家传送重新尝试。目标已在同cell的384单位内不重复搬动。
- 用户确认：固定中的NPC留在原地，不解除固定、不自动传送。牵引/绳索处理中的目标也不抢框架控制权。死亡、禁用、击杀或正在其他骑乘等状态不强搬；B还原中的目标不召回。

## 验证与部署

21项实际函数/替身C++回归通过；事件接入后重新执行受影响travel回归。新增测试覆盖：2万帧普通走路/跨格不误判、30/60/144FPS延迟、短距离finish事件、加载间隙、一次移动、固定/牵引/还原/死亡过滤、源区域卸载等待、无地面超时、存档ID重映射/错误版本/清理。Release构建及SKSE导出检查通过。

仅替换SKSE/Plugins/NPCMounts.dll，不改目录名、INI、ESP、NPC/种族、美化、动画或MO2配置。DLL SHA-256：E68C074DF0C837B672FBA499DDCB0C5DB465C27FAA83A30CEAF08C1ECC4904D7。

目标：E:\iniRePather-Skyrim\Mod Organizer 2\mods\【实验室】NPC坐骑-ai编码。
完整备份：E:\iniRePather-Skyrim\Mod Organizer 2\backups\NPCMounts-0.4.1-travel-20261016。
冻结计划：reports/travel-0.4.1-plan.json；阶段状态：reports/travel-0.4.1-status.json。

未游戏实测：需验证快速旅行、室内外过门、脚本传送、固定目标保留、B清除、存读档及狭窄落点。离线通过不证明引擎移动已成功或已排除所有加载CTD。本轮不自动提交推送。
