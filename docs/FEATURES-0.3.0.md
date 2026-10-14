# 0.3.0 新法术实现与验证（2026-10-15）

## 范围

实现C固定已转换坐骑、D束缚敌对人形、E/F调用LeashFramework创建/解除牵引，以及A～F六本法术书。移除自动AddSpell；已有法术不删。原有A/B FormID不变，唯一ESP主文件仍为Skyrim.esm，无NPC/RACE覆盖。新记录22条（另有TES4头），书籍局部ID 810—815，SPEL A804/B805/C80A/D80B/E80C/F80D。

固定状态与牵引互斥，二者均拒绝骑乘。D非转换目标不能用A直接覆盖原状态，须先B解除。B可解除D非坐骑目标；无准星目标时批量释放，未解析引用保留待释放标志。死亡目标清理但不复活。

## 实现

1. C保存位置、朝向、cell、原标志/生命状态/AI/手部装备，创建独立持久固定桩与原版绳环复制记录。MoveTo_Impl与Character::SetPosition拦截只作用于C目标；NPC更新后及主循环校正越过普通入口的位置漂移。不关闭动画AI，不改种族。正常随从MoveTo被拦截；直接写内存或其他插件覆盖钩子不在绝对保证内。
2. D仅对IsHostileToActor(player)且有兼容人形背骨的目标生效。StopCombat、restrained/移动攻击施法限制，并在束缚期间拦截涉及该目标的敌对查询和StartCombat。原阵营不改；B后可重新敌对。原版绑缚站立事件及装备实际外观尚需游戏验证。
3. E调用当前安装PEX与源码共同核对的IsLeashed、ApplyStandaloneLeash、SetRagdollOverride，独立rope NIF挂Spine2，节点Leash1_0/Leash1_1_，长度100/220，开启牵引。牵引Active时撤回restrained/MovementBlocked，否则框架CanPull会拒绝；用控制器实际速度选择已有爬行动作。禁用本次布娃娃拉拽，不改框架全局配置。
4. F只按玩家与该目标调用DisconnectLeash，不调用全局UnleashAll。Applying/Active/Stopping及请求token覆盖创建中取消、失败、断开重试与旧回调。B等待异步断开后再还原坐骑。框架OnUnleash清理本地所有权；跨读档排队通知用session隔离。
5. BIND v1为count+每条64字节，TETH v1为count+每条8字节；与NMNT v5/RIDE独立。校验长度、坐标有限性与生命状态，重解析FormID。删除引用时清固定桩。框架自身负责绳索持久化，本插件保存牵引所有权并检查是否仍存在。
6. B移除本插件增加的一件绳环，并尝试按基础ARMO恢复原手部物品；不区分同基础物品的不同附魔实例。不会改写原Race、基础派系、其他Mod配置。

## 素材与依赖

- 固定桩原样复制用户DD目录meshes/devious/furniture/kimy/restraintpost_static.nif；只引用原版RiftenCanalWood01与RiverwoodBlacksmith01漫反射/法线。C/D无需DD运行前置。模型与动画只供本地使用，未确认授权不公开分发。
- E/F需要当前LeashFramework 1.1.7与Leash.esm及其前置；未修改其源码或DLL。缺失只拒绝E，A～D保留。
- MinHook v1.3.4，官方提交c3fcafdc10146beb5919319d0683e44e3c30d537，原样静态编译，许可证随包。仅支持当前SE 1.5.97构建；不宣称AE兼容。

## 验证

Release编译及SKSEPlugin_Load/Query/Version导出通过。11组实际函数提取/测试替身回归通过：配置、下骑时序、下骑、输入、HoldPose、B/存档/scale/速度、座位、同步、动作切换、固定钩子/敌对/BIND/TETH、E/F异步生命周期。新增测试含未加载目标跨cell传送拦截、坐标钳制、绕过后修复、D敌对双向抑制、格式错误拒绝、创建前/创建中取消及旧token隔离。

完整候选包独立解析22条ESP记录及六本BOOK链接；无NPC/RACE覆盖，6996个动作路径匹配既有18段验证产物，下骑31帧资源保留。没有重新生成动作，不覆盖INI。

本轮SSEEdit自动检查没有产出新的报告；2026-10-14旧报告不算本次验证。以上不是Papyrus VM或游戏引擎实测；C快速旅行、D战斗/绑定外观、E/F绳索和爬行、存读档、与其他钩子兼容仍需游戏验收。

## 部署

冻结计划reports/features-0.3.0-plan.json。按维护技能完整备份7012个旧文件，再替换DLL与ESP、新增固定桩模型与MinHook许可证，最终7014文件；原7010文件不变。备份位于MO2/backups/NPCMounts-0.3.0-features-20261015，包含旧Mod、原modlist及改动记录。目标目录仍为【实验室】NPC坐骑-ai编码，不重命名、不修改MO2排序；运行中的MO2无需因本次文件替换而关闭，游戏必须未运行。部署结果以status及全量哈希核验为准。不自动提交推送。
