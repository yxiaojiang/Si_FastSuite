# 延期问题清单

> 这里只记录已经明确不属于当前 P0/P1 修复批次、但未来必须处理或待特定触发条件成立时处理的问题。进入对应阶段时，应迁回 `REVIEW.md` 并改为 `OPEN`。

---

## RISK-001 [OPEN] J2/J3 耦合比与回转圈数真机/ROBOGUIDE对拍未接入（P2）

- **来源轮次**：M06 / M07 系统架构复审（2026-10-26）。
- **位置**：`include/olp/algo/OpwKinematics.h`、`src/algo/OpwKinematics.cpp`。
- **现状**：当前 J2/J3 耦合比保持为 +1.0，圈数记在 J1、J4、J6 上。用户决定先不安装 ROBOGUIDE，继续无示教机/仿真软件开发。目前生成的关节姿态与 `.ls` 程序只能作为几何验证与离线草稿，未与 FANUC 官方示教器或 ROBOGUIDE 对拍。
- **后果**：真机下发时可能因真实硬件减速机连杆耦合符号相反或零位偏差导致手臂翻转。
- **触发阶段**：具备 FANUC 真实机器人或已安装并配置 ROBOGUIDE 仿真授权时。
- **验收条件**：通过真实示教器备份导出 `.va` / `.tp`，对比前向与逆向运动学姿态，小臂物理倾角误差 $< 0.01^\circ$。

---

## RISK-002 [OPEN] 回退段（RETRACT）对前侧板间隙低于 5mm 安全门禁（P2）

- **来源轮次**：Test 10 / SampleSession 端到端演示（2026-10-26）。
- **位置**：`src/ui/SampleSession.cpp`。
- **现状**：示例工位中，回退段对 `Workpiece_BoxWall_Front` 的间隙低于 5 mm，触发轻微间隙告警（`RETRACT Sweep clearance below safe distance`），但未发生硬碰撞，未阻断退出码。
- **后果**：工件放置存在微小制造公差时可能擦碰。
- **触发阶段**：工位干涉自动避让（M11 路径避障与插补平滑）阶段。
- **验收条件**：自动插入抬枪过渡点，使回退段全程对母材外表面间隙 $> 5\text{ mm}$。

---

## RISK-003 [OPEN] 客户实景与 3D PDF 输出五天熔断机制（P2）

- **来源轮次**：设计基线 `Docs/OLP_System_Design_Claude.md` §3.1。
- **位置**：工位交付与导出模块（M14）。
- **现状**：客户实景导入与 3D PDF 交互式工位报告输出处于排期后置阶段，五天熔断机制未正式开始计时。
- **触发阶段**：M14 报告导出与工程交付阶段。
- **验收条件**：支持输出内嵌 3D 模型的工程报告 PDF 或一键 Web 预览包。

---

## UI-001 [RESOLVED in M09] 主工具栏“姿态平滑”按钮支持 `targetTable_` 多选局部区间插值（P2）

- **来源轮次**：`M08-TOOLPATH-EDIT-001` Round 1 审核（2026-10-26）。
- **位置**：`src/ui/ShellWindow.cpp` 姿态平滑动作与 `selectedTargetIdsFromTable`。
- **现状**：已在 M09（`M09-PROCESS-TUNE-001`）中闭环。选中行按行号排序，多选不少于 2 点时用首尾 `targetId` 做局部 SLERP，单选或未选退化为整条焊缝。
- **闭环验证**：Test 16 中区间内 `T_3` 插到约 20°，区间外 `T_1` / `T_5` 保持单位阵且 `!userModified`。M09 Round 1 审核已复核源码，维持 **RESOLVED**。

---

## M09-004 [OPEN] 界面入口把未入栈的命令也报成成功（P2）

- **来源轮次**：`M09-PROCESS-TUNE-001` Round 1 审核（2026-10-26）。
- **位置**：`src/ui/MainWindow.cpp` 的 `smoothTargetOrientations`、`rotateTargetsTcpZ`、`modifyTargetProcessParams`。
- **现状**：M10 Round 1 在 `execute()` 之前读 `didMutate()`，成功的编辑也返回 false。正确行为改由 `M10-001` 验收：有变化返回 true 并重新验证，零变化返回 false 且不入栈。本条保留，避免修 `M10-001` 时改回“一律返回 true”。
- **触发阶段**：与 `M10-001` 同一轮。
- **验收条件**：返回值跟随执行之后的 `didMutate()`。未入栈时状态栏说明没有变化，不显示成功。

---

## M09-005 [OPEN] 焊接条件表没有编辑界面，非法条件号被静默改成 1（P2）

- **来源轮次**：`M09-PROCESS-TUNE-001` Round 1 审核（2026-10-26）。
- **位置**：`ProjectDocument::weldSchedules`；`ShellWindow.cpp` 工艺事件列解析。
- **现状**：文档里只有默认条件号 1。表内没有电流、电压、送丝、气体的编辑入口。输入 `Arc Start[99]` 或 `[0]` 时不报错，条件号落到 1。`WeldScheduleDefinition` 的速度字段若绕过 setter 直接赋值，会和 `getWeldSpeedMmPerSec()` 不一致。
- **触发阶段**：工艺参数表面板（仍属 M09 的条件表编辑，不挡当前三条 P1）。
- **验收条件**：可查看并编辑 1~32 号条件的电流、电压、送丝、气体与速度；非法条件号拒绝入栈并保留原文本；速度只通过 setter 写入。

---

## M09-006 [OPEN] 圆弧运动 C 与自转连续拖拽合并未接入（P2）

- **来源轮次**：`M09-PROCESS-TUNE-001` Round 1 审核（2026-10-26）。
- **位置**：`MotionType::C`；`RotateTargetAroundTcpZCommand::mergeWith`；属性面板步进按钮。
- **现状**：M10 自转图是点击扇区，每次点击各入栈一条。没有按下拖拽，`mergeWith` 仍默认 `allowMerge=false`。路径点表只接受 J/L。后处理遇到 C 会拒绝写出。
- **触发阶段**：为自转图加连续拖拽时，或 M11 圆弧插补。点击选角不把本条打回 `OPEN`。
- **验收条件**：扫描图上的连续拖拽合并为一条撤销命令；C 在插补与后处理可用之前，表格不把它收成一条合法运动。

---

## M09-007 [OPEN] 多选在刷新后塌成一行，工艺参数不能一次改多点（P2）

- **来源轮次**：`M09-PROCESS-TUNE-001` Round 1 审核（2026-10-26）。
- **位置**：`ShellWindow::applySelection` 的 `selectRow`；路径点表 `itemChanged`。
- **现状**：自转按钮会旋转当前多选的全部点，但刷新后 `selectRow` 只留下当前行。表格双击只改被编辑的那一行，未调用已有的 `modifyTargetsProcessParams`。
- **触发阶段**：下一次做路径点批量编辑时。
- **验收条件**：一次速度或 CNT 编辑可作用于当前多选行，并作为一条命令入栈；操作完成后多选范围仍在。

---

## M09-008 [OPEN] 路径反向把 processModified 和工艺事件拆开（P2）

- **来源轮次**：`M09-PROCESS-TUNE-001` Round 2 审核（2026-10-26）。
- **位置**：`ReverseOperationPathCommand::applyReversed`。
- **现状**：`processModified` 随运动、速度、CNT 在起弧点与收弧点之间对调。`Arc Start` / `Arc End` 留在原来的点上，只改事件类型，并把 `arcLengthRatio` 取补。之后再重生成时，手改过的条件号可能因为标志已经离开该点而被默认规则覆盖；对面那个点却带着 `processModified` 把默认事件保留下来。
- **触发阶段**：下一次改路径反向，或反向之后立刻重生成。
- **验收条件**：反向后再重生成，手改过的条件号、CNT 和速度仍在翻转后的对应点上；没改过的一端仍按生成规则重写。

---

## M09-009 [OPEN] 速度单元格把非整数 mm/s 显示成整数（P2）

- **来源轮次**：`M09-PROCESS-TUNE-001` Round 2 审核（2026-10-26）。
- **位置**：`formatSpeedCellText`。
- **现状**：`50 cm/min` 正确存成 \(8.333\) mm/s，表格用 0 位小数显示成 `8 mm/s`。用户再提交这个显示文本时，速度变成 8。
- **触发阶段**：下一次改速度列的显示格式时。
- **验收条件**：显示至少保留到 0.1 mm/s，或提交显示文本后内部速度与换算结果之差小于 \(0.05\) mm/s。

---

## M10-004 [OPEN] 可操作度是几何代理，不是 Yoshikawa 指标（P2）

- **来源轮次**：`M10-COLLISION-REACH-001` Round 1 审核（2026-10-26）。
- **位置**：`OpwKinematics::checkSingularity`；属性面板「可操作度」。
- **现状**：指标是 \(|\sin q_5|\cdot|\sin(q_2+\psi)|\cdot\min(1,|r|/50\,\mathrm{mm})\)。注释已写明不是雅可比最小奇异值。腕奇异（\(q_5=0\)）和肘伸直能把该乘积打到 0。Home 实测约 0.974。交接说明把它称作 Yoshikawa 可操作度。
- **触发阶段**：对外把该数字当作与雅可比一致的可操作度，或做奇异裕度优化时。
- **验收条件**：在腕、肘、肩三处与数值雅可比最小奇异值同时趋近 0；远离奇异时两者的数量级一致。在此之前界面不要把该乘积标成 Yoshikawa。

---

## M10-005 [OPEN] Test 17 有三处不会在回归时失败的断言（P2）

- **来源轮次**：`M10-COLLISION-REACH-001` Round 1 审核（2026-10-26）。
- **位置**：`tests/test_main.cpp` 的 `testKinematicsReachabilitySingularityAndTwistScan`。
- **现状**：肩部那组关节只断言可操作度落在 \([0,1]\)，没有断言 `isSing`。构型锁失败时走 `check(true)`，若返回了解，else 分支只核对 flip，并不要求空解。非焊接接触写成 `collided || clearance < 5`，间隙 4 mm 的近失也会通过。本轮 Home 锁构型确实返回了空解，所以套件是绿的。
- **触发阶段**：下一次改奇异判定或焊丝碰撞豁免时。`M10-002` 要求的 MainWindow 扫描测试不替代本条。
- **验收条件**：肩奇异样本必须 `isSing`；锁不住的构型必须 `!has_value()`；非焊接尖端接触必须 `collided` 且间隙 \(\le 0\)。

---

## M10-006 [OPEN] 点击分支切割点会得到没有样本的 +180°（P2）

- **来源轮次**：`M10-COLLISION-REACH-001` Round 1 审核（2026-10-26）。
- **位置**：`TwistScanWidget::mousePressEvent`。
- **现状**：采样停在 \(+175^\circ\)，\(-180^\circ\) 与 \(+180^\circ\) 是同一姿态。点击负 X 轴时 `atan2` 得到 \(+180^\circ\)，`deg <= -180` 又会把 \(-180^\circ\) 折成 \(+180^\circ\)。随后按绝对角下发，样本序列里没有这一格。
- **触发阶段**：下一次改自转图的点击或拖拽取角时。
- **验收条件**：点击分支切割点得到 \(-180^\circ\) 或 \(+175^\circ\) 中与指针最近的已有样本，不发出没有样本的 \(+180^\circ\)。

