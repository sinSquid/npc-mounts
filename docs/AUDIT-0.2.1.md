# 0.2.1 实现逻辑自检（2026-10-15）

## 发现并修复

1. 下骑：0.2.0只在进度达到1后等待一次Player更新，不能证明异步动画任务输出过末帧。现由实际GeneratePose成功写入末帧确认，再完成下骑/B还原。无输出最多额外等待0.25秒，警告后解除，保留任意地点下骑。输出写入与Clear互斥，避免正在执行的回调在清理后继续写姿态。
2. 座位：Update在3D、背骨缺失或缩放无效时曾直接return，旧目标仍active。现停用旧输出，资源恢复后重新计算；没有修改已发布的NiNode或进行额外场景图向下更新。
3. 缩放：此前逐帧比较GetScale与任意精度配置后SetScale，若引擎量化/返回差异超过0.001可能反复重设。改为仅启用、配置变化或读档后写入一次；原scale仍持久保存，关闭覆盖/B恢复。会话缓存不加入co-save布局，不变更v5格式。
4. 速度：此前差值小于阈值时不写ActorValue，却更新自身账本，使还原扣除未曾应用的数值。现仅在实际修改时更新账本；还原即使残值小于阈值也撤回。不改基准速度、不覆盖其他加法修饰。
5. INI：重读不存在/打不开的文件曾返回默认值并提示成功。现显式报告读取失败，保留当前配置和草稿；支持BOM开头的第一项，保存使用经典小数点及float的max_digits10，避免滑条小数保存后截断。未覆盖用户现有INI。

## 六项检查结论与证据

- 下骑动画：包内Dismount.nmp为31帧、99骨，从骑坐经抬腿到站姿；复核生成脚本与实际采样。新增测试执行Tick真实下骑分支，30/60/144FPS验证末帧确认、超时释放、B顺序及保留竖向速度。不是游戏视觉验收。
- scale座位：新增直接执行Seat::Update测试，而不是只预设target；覆盖NPC0.5/1/1.24/3、玩家0.65/1/1.5、多个朝向、SaddleBone优先/Spine2回退、偏移、模型根滞后、下骑终点及失效后停用。原GeneratePose测试继续覆盖7个阶段和非法轨道。
- 速度/B：实际ApplyStats/ApplySpeed/Restore测试验证1000次不叠加、极小差值账本、其他修饰保留、scale配置变化/关闭、B恢复、v1—5迁移；序列化替身支持多个记录，实际Save/Load同时往返NMNT与RIDE。仍非游戏存档引擎测试。
- NPC动作：reports/animations.json的sourceMode为human-crawl；实际生成分支读取F NPC Mount人形99轨爬行动作，并固定背部锚点，空中保持四足姿态。报告内旧mapping是未使用的马重定向分支元数据，不代表当前NPC动作来源。重新解码10段NPC动作与骑手片段，各帧锚点/四足idle/运动检查通过；已部署6996路径哈希均匹配18段验证产物。
- NPC种族：实现无SetRace/ActorBase替换；ESP只有2个FACT、2个MGEF和2个SPEL，唯一master为Skyrim.esm，无NPC_或RACE覆盖。转换前是人类则仍是原人类种族；精灵、兽族、自定义人形也保留自身原种族，不统一改为HorseRace。转换约束按兼容人形骨架，而非强制只接受人类race。
- INI/菜单：实际读写测试增加缺失文件失败、BOM、任意精度float往返。核对Menu Framework本地API签名；当前MO2仅目标Mod提供NPCMounts.ini，未发现Overwrite的同名配置。不改其已有取值。

## 原生接口与验证边界

行为图Generate的0x17槽位及五参数签名与本地OAR官方源码Hooks.cpp一致；CommonLib头部的单参数声明不完整，所以保留显式函数指针签名。输出轨道布局与CommonLib hkbGeneratorOutput匹配。
目前运行日志仍来自旧版，没有0.2.x实际运行证据。DLL编译、真实函数测试替身、动作解码和部署哈希不等于游戏中姿态、SMP、镜头或存档恢复已验收。
部署仅替换NPCMounts.dll，动画、INI、ESP、MO2配置和目录名保持；计划/状态见reports/audit-0.2.1-*.json。完整备份为MO2/backups/NPCMounts-0.2.1-audit-20261015。不自动提交推送。
