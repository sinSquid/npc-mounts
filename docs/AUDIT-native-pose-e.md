# 2026-10-16 原生日志复核与修复 E

## 证据与边界

复用 native-pose-e/NPCMounts.log 和此前 C/D 备份，没有要求重新复现。
23:19—23:22 日志中 FF016DE6、FF01660B 转换后无上骑入队，而 FF016F83 有入队及 Mounted；不能据此证明 DoM 吞键。位置同步日志不是实际画面无抖动的证明。

确认旧包把原生走路/跑步片段替换成同一个 9 秒静态骑姿（对照原版走路约 1.13 秒、跑步约 0.63 秒）。它改变的是行为图实际播放片段，不是单纯最后一层姿态。旧座位坐标修正不能解决这一设计缺陷。

## 本次修改

- 禁用本包全部 11 个 OAR 子模块，保留资源便于回退；构建脚本同步生成 disabled=true。不禁用其他 Mod 的 OAR。
- 玩家骑姿及上下骑由 Seat 最终姿态输出负责，保留原生移动片段、提取运动、根/辅助和相机轨道，额外骨骼不覆盖；NPC 仍由 Crawl 输出人形爬行。
- 交互回退新增相机中心物理拾取，不依赖原生准星可激活标志；仅最近实体、距离不超过 200、已转换且可骑目标。排除玩家及触发层，墙体仍阻挡。
- 修正 bhkWorld::PickObject 射线输入：起点、零终点及独立位移 ray 字段；原函数遗漏 ray。共用落点检测一并修复。
- 不改 DoM 文件、NPC 种族、INI、ESP，不强行夺取 AI/外部传送。不增加持久化字段；B 沿用现有排队下骑与恢复账本。

## 开源依据

- https://github.com/ersh1/OpenAnimationReplacer/blob/main/src/ActiveClip.cpp ：替换实际动画片段和触发信息。
- https://github.com/ersh1/OpenAnimationReplacer/blob/main/src/Parsing.cpp ：子模块 disabled 字段。
- https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/master/SmoothCam/source/raycast.cpp ：PickObject 射线位移和过滤收集器约定。
- 本地 CommonLibSSE-NG 头文件核对结构/虚函数及碰撞引用 API。

## 验证范围

Release 编译、提取生产函数的离线回归、包资源/部署哈希核验。新增姿态保留、独立拾取与射线构造/收集器测试。测试替身不模拟真实 Havok、相机、DoM 或 OAR 运行；本轮不能宣称游戏故障已全部消失。保留诊断日志供后续自然游戏会话使用。

部署计划：build/native-pose-e/plan.json；完整部署前备份：E:\iniRePather-Skyrim\Mod Organizer 2\backups\NPCMounts-native-pose-20261016-e。
