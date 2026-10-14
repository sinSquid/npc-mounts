# NPCMounts

人形坐骑法术，当前为 **0.1.5 逻辑修正候选版**，只构建 SE 1.5.97。
0.1.0、0.1.1及0.1.3均有用户确认的故障。0.1.3实测玩家移动而NPC留在原地、无动作；0.1.4修正控制器同步及更新顺序，并加入每次骑乘最多8条同步日志。已离线检查，尚未游戏复测，不能标记为可用正式版。详情见 docs/REVIEW.md。
0.1.5源码审查修正动作派系等级晚于动画事件发布、死亡/剧情自动还原被普通下骑条件阻挡的问题。尚未证明这些修正解决了NPC不跟随或无动作的主要故障。

## 操作

- `A·人形坐骑化`：瞄准一个已加载、存活、使用兼容人形骨架的 NPC 施放。
- 对转换目标按游戏的交互键：骑乘；骑乘中再按一次：下骑。
- 使用正常移动、奔跑和跳跃键。跳跃不被禁用。
- `B·解除坐骑化`：优先还原当前坐骑，其次还原准星目标；准星没有目标时恢复全部转换。
- 空中不能普通下骑；B会保留还原请求，落地并有安全位置后自动完成。墙边没有合适落点时移到开阔位置即可，无需重施B。无目标B对未加载引用保留待还原请求，读档也不会重新接管该目标；卸载Mod前须让还原实际完成。
- 剧情场景、击杀动作、家具中或正在骑其他坐骑的 NPC 暂不接受转换。

加载存档后自动添加两个法术。转换标记保存在 SKSE co-save；读档保留转换，但不自动恢复骑乘。
卸载前用 B 的无目标操作解除全部转换，保存并退出。不能承诺直接删 DLL 仍能恢复暂停的 NPC。

## 实现

- NPC 的种族、性别、身高、外貌、装备、派系原记录均不改。
- 给目标引用增加本插件的独立标记派系；保持 AI 启用，用可恢复的 restrained 状态约束自主行为，避免 EnableAI(false) 冻结动画。B停止冲刺/移动、移除标记后恢复原生命状态、AI开关和接管标志；不覆盖已经死亡目标的生命状态。co-save版本4保存待还原请求，兼容读取版本1/2/3。
- 18 段 99 轨人形兼容动画：马来源的四足动作、原版骑手姿势、保留跳跃事件的骑手片段及中性附加层。
- OAR 按引用派系等级选动作；普通 NPC 没有该标记，不匹配。
- 玩家原生控制器负责行走、墙体碰撞和跳跃。坐骑保留物理模拟，只抑制角色间碰撞；位置改变时通过SetPosition(true)同时同步引用和物理控制器，玩家及该NPC更新后均校正同步。按玩家物理控制器的水平速度选动作，步态阈值有滞回，转向按弧度/秒判定；暂停时不改动作状态。只在运动状态变化时发送移动/冲刺事件，不每帧重置动作。
- 骑手坐姿的位置写入动画 COM 轨道，镜头轨道保留正常人形待机值；不再在更新前后移动任何骨骼节点或重复向下更新场景图。坐姿按标准人形四足参考制作，极端身高/体型的座位贴合仍需另行校准。
- 不调用原版人马配对上下马动画。对话启动、强制招呼及标准 DialogueMenu 对转换目标被拦截。
- 不删除其他 Mod 的对话记录，不保证拦截第三方自定义界面或绕过正常入口的脚本。

运行时依赖：SKSE 2.0.20、对应 Address Library、OAR。无需 F NPC Mount、Paola、Piggyback 或 RSE。
未增加行为图，不需要为本包重新运行 Pandora。保留 OAR 预加载；当前整合的重复动画过滤已开启。
18 个实际动画由多个路径复用；这些路径不是 2508 段不同动作。

运行日志：Documents/My Games/Skyrim Special Edition/SKSE/NPCMounts.log。0.1.4记录玩家更新钩子首次执行；每次骑乘最多8条、间隔0.5秒记录双方位置、NPC模型根位置、NPC更新次数、同步前误差、玩家速度和期望/实际派系等级。日志用来定位运行时故障，不代表功能验收。

## 构建

需要 Visual Studio 2022 C++、CMake、Node，以及已安装 CommonLibSSE 的依赖前缀：

```powershell
.\tools\build.ps1 -Dependencies '<CommonLib CMake prefix>' -SkyrimData '<Skyrim Data>'
node .\tests\verify-package.cjs
```

`build.ps1` 生成 ESP 和 DLL；动画需先使用 `tools/build-animations.py` 生成到 `dist`。
离线动画工具参数可运行 `python tools/build-animations.py --help` 查看。
工具依赖 numpy、lz4、bethesda-structs、PyNifly HKX 编解码器、HKX2 转换器及 hkxcmd；它们不是游戏前置。
校准姿势通过 `--calibration` 指定，目前本地生成使用用户已有的 F NPC Mount 四足待机。
生成后的游戏包不读取该 Mod。未经确认素材授权，不发布这些生成动画到公共仓库。

`dist` 是完整 MO2 候选包；构建脚本不更改游戏或 MO2。编译报告在 `reports`，完整方案复核在 `docs/REVIEW.md`。

## 源码参考

- Piggyback：<https://github.com/Kroosstiich/piggyback>，参考逐帧更新、附着和控制器释放问题。
- RSE：<https://github.com/Hunk92/RSE-Shoulder-Or-Saddle>，参考碰撞管理思路。
- OAR：<https://github.com/ersh1/OpenAnimationReplacer>，核对 JSON、派系条件和中断配置。
- xEdit：<https://github.com/TES5Edit/TES5Edit>，核对 MGEF/SPEL/FACT 记录布局。

没有把这些项目的 DLL 或源文件打包为游戏前置。本地骨骼与动作源不代表获得其公开再分发授权。
