# 用户再次复现失败后的调查

此前两轮没有解决用户实际报告的两个问题，不能以22组替身测试作为游戏成功证据。本轮不标记修复完成。

## 开源对照
- TDM 官方接口与实现：https://raw.githubusercontent.com/ersh1/TrueDirectionalMovement/master/src/TrueDirectionalMovementAPI.h ，https://raw.githubusercontent.com/ersh1/TrueDirectionalMovement/master/src/Hooks.cpp 。V1虚函数次序与本地接口一致，日志handoff=0，不支持“接口顺序错”的判断。
- SmoothCam 实现：https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/master/SmoothCam/source/crosshair.cpp 。目标拾取与显示/投射射线是独立环节，已有target/targetActor不能保证被其他系统控制的NPC一定可拾取。没有未经验证地加入宽角度自动选择或隔墙选人。
- CommonLib hkbGeneratorOutput 明确分离WorldFromModel、ExtractedMotion和Pose；修改Pose不等于消除角色控制器或根运动的影响，不应直接清零ExtractedMotion以免阻断正常移动。
- 本地Skyrim-Leash-Framework/src/Movement/DirectLocomotion.cpp显示其使用运动规划接口并分别记录控制器与动画驱动状态。借鉴其观测字段，未复制其私有虚函数调用或直接接管现有NPC。
- 只读检查本地DoM的交互Perk、Keys和Animator涉及的控制状态，不修改DoM脚本/所有权。

## 本次日志
23:21:07和23:21:44转换FF016DE6/FF01660B后无上骑入队记录；23:22:12 FF016F83有入队且Mounted成功。失败分支此前没有日志，不能据此确认是输入未到还是拾取失败。
无输入时出现约49单位水平速度，部分转向期间速度剧变；不能单凭采样把正常惯性/地面作用与异常驱动区分。仅修正座位坐标不能解释全部运动异常。

## 诊断D
仅增加日志，不再改变座位/移动算法：Activate未入队时记录映射、上下文、普通/回退目标和Converted状态；骑乘限量采样窗口中，记录玩家/NPC原生Update前后速度、输出速度、pushDelta、接触数、动画驱动与运动规划模式。新增编译日期/时间识别实际加载版本。不会拦截其他Mod的AI/传送。

局限：某个更早输入监听器删除事件时，本监听器不能记录该按键；日志缺失须与正常目标按键对照，不能当作玩家没按键。运动阶段观测不等于堆栈归因。
