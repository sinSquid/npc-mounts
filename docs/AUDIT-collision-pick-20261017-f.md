# 2026-10-17 碰撞与拾取 F

## 已有证据

复用 2026-10-17 00:16—00:22 的 NPCMounts.log。E 构建时间 23:47:19 确认已加载（旧启动文字仍写 D，本版更正标记）。用户反馈 E 仍未解决两个问题。

- FF016DE6 转换、爬行输出后，E 键输入进入本插件，但 selected/fallback 均为零；不能归因为 DoM 吞键。
- FF016E16 的多个无移动输入样本速度约 49.27，缩放由 1.442 降至 1.061 后仍约 43.55，控制器存在接触和空中状态切换。
- Serana 02002B74 停止后速度归零，17 个状态采样均为地面。两者都有爬行输出与相同额外图骨骼警告，后者不是区分原因。
- 五人组使用独立骨架与三个自定义人形种族，但仍使用原版人形行为图。其骨盆/上背节点局部缩放 0.86/0.9；自定义种族本身不能证明故障原因。
- 当前 profile 启用 Disable Follower Collision。不能假定所有目标会得到相同第三方碰撞处理。

## 代码缺口及处理

1. 旧实现只设置控制器 NoCharacterCollisions，未实现当前玩家/坐骑物理组对的过滤。F 在 bhkCollisionFilter 的 hkpCollidableCollidableFilter 子接口中过滤这一个非零、不同的双向组对，其他组合调用原函数，兼容原有钩子链。组号由控制器读取，上骑重叠前发布、玩家更新时刷新，正常/紧急下骑与序列化 Revert 清除。不改全局碰撞矩阵、碰撞物体字段或存档格式，不关闭地面碰撞。
2. 旧拾取仍依赖物理碰撞体存在并参与射线。F 仅在物理拾取无有效目标时，对已转换且200单位内目标的骨盆、上背、头节点做窄球射线检测，按命中深度选取。不自动选择最近 NPC；排除死亡、待恢复、未加载或不同 cell 的对象；骨骼中心及命中点必须在墙体遮挡前。
3. 修补 MountRay 未转存 collector.hitFraction 的缺口，遮挡深度不能沿用默认值。增加失败时附近目标 BaseID/race 与已发布物理组及过滤命中数日志，沿用限时运动诊断范围。

开源核对：CommonLibSSE-NG 的 bhkCollisionFilter/CFilter/hkpCollidableCollidableFilter；https://github.com/VersuchDrei/ImWalkinHere/blob/master/src/CollisionHandler.h 展示队友判定和独立碰撞物体路径，本版没有复制它的临时碰撞字段写法。

## 自检与限制

第一轮：原有24组生产函数回归、新增可见拾取与组对过滤回归；修正新增测试夹具语法/函数范围定位问题。第二轮：完整26组回归，检查上下骑/B清理、存读档、控制器重建组号变化、原函数转发、墙体边缘以及10000次组对设置/清除。

这是针对明确代码缺口的候选修正，不是已经证明所有 Havok 接触均来自当前坐骑。离线测试不验证真实 Havok 缓存、第三方钩子调用顺序、DoM运行状态或画面。不得将两轮自检通过写成游戏验收通过。

不修改五人组、Serana、DoM、INI、ESP、OAR或骨架资源；只部署NPCMounts.dll，目录名保持。完整备份及冻结计划位于 backups/NPCMounts-collision-pick-20261017-f 与 build/collision-pick-f。
