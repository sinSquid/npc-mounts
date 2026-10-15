# 0.3.3 — 2026-10-15

本轮对0.3.2再次检查，实现三项确定修复：

1. StopTether原回调忽略DisconnectLeash的false结果而删除本地记录。现在false后进入Reconciling，至少1秒后查询GetLeashHolder；确认目标不再由玩家持绳才清理本地记录，仍由玩家持绳则有限重试断开。最多累计3次失败，查询成功不清零断开失败计数；超时或故障保留既有Failed处理，B可恢复本地状态并提示绳索未确认清理，F可人工触发重新确认。未修改框架源码。
2. TESObjectLoaded事件原地修改Actor，现排入游戏任务队列，执行时核验存档会话、Actor身份、cell附着与3D加载。静态物体、卸载事件不产生任务。C/D周期维护与加载还原也等待就绪，普通坐骑属性写入及读档接管增加相同条件。避免在加载线程直接改装备、AI或动画；加载未完成时下一更新/加载任务继续处理。没有添加传送阻挡或坐标回拉。
3. C/D Release原来仅在当前Restrained时恢复保存的生命状态；若外部脚本将其改为Alive，原来保存的Restrained会丢失。现在Alive/Restrained两种受控状态均还原，死亡及其他生命阶段不覆盖。

14组原生实际函数/替身回归通过。新用例覆盖断开false但绳索仍存在/已经消失、三次失败停止、确认查询不重置失败计数；加载事件先排队后修改、旧存档任务失效、未附着/无3D不接管、待B还原优先、非Actor不排队；C/D原受限状态及未加载AI保持。Release构建成功。

保存布局仍为NMNT v5、RIDE v1、BIND v2、TETH v1。XPMSSE前99骨兼容检查、额外轨道保留及其他骑乘/INI/速度/下骑逻辑保持。需游戏复测快速旅行、跨cell加载、C固定、E/F与B；离线验证不能证明所有CTD已经消除。

部署至原`E:\iniRePather-Skyrim\Mod Organizer 2\mods\【实验室】NPC坐骑-ai编码`，完整7014文件备份位于`E:\iniRePather-Skyrim\Mod Organizer 2\backups\NPCMounts-0.3.3-audit-20261015`；_evidence含原维护记录、modlist及运行日志。仅替换DLL，7013原文件保持，全量哈希及独立包结构验证通过。更新维护记录原第84项，目录、INI、ESP、OAR资源及MO2配置保持。DLL SHA-256：`06B6EEB9D9ADB34583447EBEB171FA4E715B81D06BE42768BE9DBD74609D59C3`。本轮未自动提交推送。
