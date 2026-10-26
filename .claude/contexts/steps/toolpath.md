# 焊缝、碰撞与仿真

1. **焊缝采样补上终点。** 起点和终点各裁 5 mm，末点一定写入。只有一个采样点时复制一帧，这样同时有弧焊开始和弧焊结束。
2. **手工改点按身份使用一次。** 接近点和回退点按角色匹配；焊接点按弧长比例匹配。用过的修改不再套到下一点。
3. **反向先做快照再改。** 反转点序后，在角色名还是旧名字时交换接近/回退和焊缝起终点的运动方式，再改名、翻转弧光事件、弧长比例取补，并绕 Z 转 180°。撤销恢复快照。没有改到文档的命令不入栈。
4. **焊丝尖端只豁免本焊缝的两块母材，而且只在约 10 mm 半径内。** 夹具和其他工件不自动豁免。更深的侵入仍算碰撞。球体都在枪轴上，绕枪轴扭转不会改变间隙，不能把它当成真实网格扫描。
5. **仿真只写 `TargetCache`。** 不回写目标上的配置字。无效逆解、超限位和锁死构型分开记录。圆弧运动不写成直线。
6. **演示夹具是真实干涉，用后移起弧点解决目标点。** 前侧板底边的角平分线指向 `Table_Clamp_Block_1`。5 mm 起弧时，枪尖后 45 mm 的喷嘴球吃进夹具约 9 mm。`extendStartTrimUntilTorchClears` 沿焊缝把起点收到 32 mm，枪姿不变，目标点安全间隙 5 mm 满足。插补采样也查焊枪球，终点那一帧不再重复计。零位到接近点改由 `insertSafeTransitBeforeApproach` 插入正上方的直线点 `CLEARANCE_1`，再直线下降，不再扫过 `Workpiece_BoxWall_Front`。演示退出码 0。回退段对这块板的间隙仍低于 5 mm。
7. **复合曲率焊缝累计弧长采样与动态角平分线姿态（M07）。** `buildSeamFromEdgeChain` 沿边链累计弧长均匀离散采样，保留 `isClosed` 标志，对每个采样点调用 `evaluateFaceOutwardNormal` 动态求取两侧母材面（含圆柱/圆锥/圆角曲面）的局部外法向，生成随曲率平滑旋转的角平分线右手姿态矩阵（$\det(R)=1.0$），邻接面缺失时按拓扑最近邻继承。`CreateSeamFromPickedEdgeCommand` 在 `chain.edges.size() > 1 || chain.isClosed` 时自动创建链式/闭环焊缝（避免单边圆环被默认首尾裁剪 10 mm），并拦截重复拾取同一边链（No-op 不入栈），保证 Undo/Redo 与 dirty 状态严格对称。
8. **路径生成智能抽稀与多点姿态 SLERP 平滑（M08）。** `generateOperationTargets` 引入严格的非闭环单段直线几何判定（`!seam.isClosed && seam.edgeChain.size() <= 1` 且各采样点离弦距离 $\le 0.05\text{ mm}$、切向夹角一致），仅真直线在 `endpointsOnlyForLine = true` 时退化为首尾两点（4 点模式）；圆环、多段边链与非线性曲线焊缝完整保留全部 `WELD_VIA` 离散采样点。切入段（APPROACH）与切出段（RETRACT）沿焊枪 $-Z$ 轴（焊丝反向）按设定距离退避，支持独立配置速度与运动方式。`interpolateOrientationsSlerp` 与 `Mat3::slerp` 对选定首尾点之间的中间点提取局部相对偏转，沿累计弧长比例 $s$ 进行 SLERP 球形线性插值并标记 `userModified = true`，使重算时自动继承平滑姿态。
