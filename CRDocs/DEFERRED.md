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
- **现状**：命令被零变化门禁拒绝、或局部 SLERP 没有中间点时，函数仍返回 true。状态栏显示已完成或已更新。
- **触发阶段**：下一次改这三条界面入口时。
- **验收条件**：返回值跟随 `didMutate()`。未入栈时状态栏说明没有变化，不显示成功。

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
- **现状**：路径点表只接受 J/L。后处理遇到 C 会拒绝写出。`mergeWith` 默认 `allowMerge=false`，界面没有滑块，连续点击每次各入栈一条。自转角扫描图属于 M10。
- **触发阶段**：M10 自转角扫描，或 M11 圆弧插补。
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

