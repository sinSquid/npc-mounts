# J：接地、坐标时序、转向及骑乘跳跃

后续更正：游戏实测发现J版动作库加载失败。原测试绕过Install，不能证明可加载；Gunslicer动作库已由K版替代，详见AUDIT-crawl-load-20261017-k.md。本报告的资源与验证结论为历史状态，不代表当前可用性。

2026-10-17。基于用户已确认步态问题解决的49fe71a。Motion::RideState与步态迟滞/周期保持算法不改，DoM备用拾取仍撤销。

## 六项改动

1. 使用启用列表中的Gunslicer Animations All in One OAR 1.15.4 / Sneak 2女性四足动作。原始idle的手关节约4、脚关节约6、背骨约39模型单位，区别于其他子包手部33—57的普通潜行。复制3个可解码原始HKX到本Mod的非OAR Sources目录；当前转换器对后退文件出现约480单位接触高度变化，转身文件为不支持的hktagfile，未将这三份副本部署。后退由前进反向采样，转身保持四足idle由角色朝向驱动，冲刺/跳跃复用跑动。未认定异常一定属于原作者文件损坏。
2. 新Crawl.nmc不再使用旧的固定背部COM处理；按每段动作最低手脚关节高度做一次常量接地校正，最低关节基准3模型单位，不把运动中抬手/抬脚逐帧拉平。此为骨骼接地基准，不保证任意替换网格的皮肤表面精确触地，也没有新增坡面四肢IK。基础99骨输出继续保留目标扩展骨骼。来源156轨为identity binding，提取基础99轨；不更换NPC种族或骨架。
3. Crawl::Anchor提供实际采样姿态的模型空间背点，Seat用双方模型比例和配置朝向差计算位置，不再用“NPC已发布背骨世界坐标－玩家另一个时刻的根世界坐标”。自定义SaddleBone保留自身模型空间锚点。微小局部高度变化做单调指数平滑，空中/大偏差/3D重建/上下骑重置或绕过；真实地形高度、碰撞与跳跃轨迹不平滑。角色原生更新仍有帧顺序，不能据此声称所有显示延迟为零。
4. 骑乘位置持续写入收敛到NPC原生更新前；上骑首次强制同步保留，删除玩家Tick和NPC更新后的重复同步，后者只观察误差。避免生成骨骼/SMP后再次搬动控制器。未添加新的碰撞接口、跨cell拉回或关闭地形碰撞。若原生更新仍产生持续位置偏差，现有分阶段日志可观察，不能以引用一致测试冒充游戏同步验收。
5. A/D按游戏左右移动动作名转换为转向，吞掉横移输入，W/S仍走原生移动；手柄左摇杆横轴同理。转速1.5 rad/s，帧率无关，双方向抵消，上下骑锁定；菜单/结束/读档清理。使用PlayerControlsData.fovSlideMode保持鼠标观察，保存原值并在下骑/Revert归还。参考SmoothCam game_state.cpp中InPOVSlideMode以及thirdperson.cpp对该状态的说明；未新增相机虚函数钩子。https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/master/SmoothCam/source/game_state.cpp
6. 骑手全部基础姿态轨道（含根和相机）输出固定骑姿/上下骑姿态，不继承原生后退与跳跃的姿态偏移；独立extracted-motion轨道不改，原生控制器仍驱动组合跳跃。空中/待起跳不接受第二次Space请求，保留此前确认起跳/稳定落地的步态逻辑。目标扩展骨骼、装备SMP配置和原生重力不改；不能保证第三方强写动画的所有入口都被拦截。

## 测试与资源

新增RideDrive生产函数测试及单一同步调用位置检查；座位测试覆盖独立根漂移不影响模型锚点、比例0.5—3、局部高度单调平滑与空中/重建绕过、故意污染原生跳跃根轨道仍输出骑姿、extracted motion和扩展骨保持。生产Crawl绑定/采样用本次真实7段bank，另核验三份复制文件哈希与前进/反向映射。原步态、B还原、存读档、上下骑、牵引及独立MASM碰撞ABI回归保留。

生成命令需要build-crawl-motion.py --crawl-dir <本地Sneak 2副本目录> --oar-sneak；完整build.ps1新增必需CrawlSource参数，防止误重建回旧固定背部动作。原始第三方资源仅本地部署，不加入Git再分发。源包条件仅女性玩家，当前使用的是独立提取资源，不依赖其条件或插件。

## 部署边界

冻结计划及测试日志：build/ride-j。暂存：build/stage-ride-j。
完整I版备份：E:\iniRePather-Skyrim\Mod Organizer 2\backups\NPCMounts-riding-six-20261017-j。
已部署原指定目录【实验室】NPC坐骑-ai编码；改DLL、Crawl.nmc及新增5份本地来源文件，INI/ESP/既有OAR配置/骨架/其他Mod/profile不变。全量7020文件冻结计划核验通过；两轮各26组回归及独立MASM ABI通过。包测试初次将非活动Sources/source-config.json误计入OAR配置，已限定旧OAR目录验证，其6996份资源和12份配置保持原校验；新来源单独逐字节及映射检查通过。全局记录131—135项已更新。

DLL SHA256：A6DE4826E75C71BE18F49C5249E1055F6C8FFE78CBD7D29901C2967C5912DAC5。
Crawl.nmc SHA256：C9117AF5240D5588BE328EBA5A600A02DF5FC8896DEE8A4BD11D3C3F13ABB6B7。
未提交推送。离线测试不等于游戏六项验收，待实际观察悬空、反向/地形/SMP表现与相机兼容。
