# K：动作库加载回归修复

2026-10-17。只处理动作资源、转换期校验、错误提示与真实加载测试，不改J版移动/转向/座位/步态/碰撞行为，不恢复DoM兼容。

## 原因及替换

03:07游戏日志显示Crawl motion data missing or invalid; conversion disabled。J版Gunslicer Sneak 2转换产物含非正缩放（例如左脚-3445.288），严格加载器正确拒绝；Bind统一错误提示误报目标骨架不兼容。旧测试自行解析文件并设置loaded=true，未验证Install，因此J版“通过”不能证明资源可加载。未证明异常来自作者资产还是解码器，不对两者妄下结论。

改用本机启用的Diary Of Mine 8.5.5b_SE-自优化所带Walk On Four/Crawling，原包标注DarkAngel and TAK。仅复制三份共享人形爬行动作（可用于女性），不复制条件、脚本或插件，不引入DoM前置。Crawling Down候选未通过接地幅度校验而被淘汰；当前Crawling三文件均通过正缩放及原有接地检查，不修改原始HKX。

转换器只新增一条与运行时一致的非正缩放拒绝断言，原J源在生成阶段失败且不输出动作库。不把负数取绝对值，不统一强写scale=1。后退反播前进、转向idle、跑动复用关系与J版相同。旧Gunslicer来源副本留作非活动历史材料，当前bank不再使用。

## 最小代码改动

- Install允许传入文件路径，默认游戏路径不变；原有格式/父索引/长度/有限数/四元数/正缩放/尾部校验完全不变。
- Available只返回已有loaded状态，用于Bind失败时区分动作库加载失败与目标骨架错误。
- 测试提取并执行生产Install本体，使用实际bank文件，只模拟最后的引擎vtable写入。删除测试手动loaded=true。缺失、截断、错误头、尾部多字节、非法父索引、负缩放、零缩放、NaN均应拒绝且不安装钩子；实际J版bank也拒绝，新bank接受。

## 构建与部署

Release DLL：7ADE54BCCE0459BC3DC8753C2B2F5C8C06F16EB62E029538F83A4EC27CCEA9D0。
Crawl.nmc：93E5CCB93E81F29B2FBACCF7A590FEC0614A6006184104D1A156A9426DEA0172。
暂存build/stage-ride-k；冻结计划、回归日志及原记录在build/ride-k。
完整J版7020文件备份：E:\iniRePather-Skyrim\Mod Organizer 2\backups\NPCMounts-crawl-load-20261017-k。
部署计划只替换DLL/Crawl.nmc并新增三份原始动作与来源说明，共6文件。INI/ESP/目录名/profile/其他Mod均不改。最终部署状态见build/ride-k/status.md；未游戏实测，不宣称SMP/悬空/抖动已解决。
