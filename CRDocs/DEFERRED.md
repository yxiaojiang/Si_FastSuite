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

## RISK-003 [RESOLVED in M15] 客户实景与 3D PDF 输出五天熔断机制（P2 -> CLOSED 熔断收口）

- **来源轮次**：设计基线 `Docs/OLP_System_Design_Claude.md` §3.1。
- **位置**：工位交付与导出模块（M14）。
- **现状**：**【RESOLVED / CLOSED 熔断收口】** 经 DIR-001 拍板定案，正式结束 3D PDF 的五天熔断悬置状态：明确不再投入资源自研高脆弱度且受限于 Adobe Reader 专有沙箱环境的开源 U3D/PRC+JavaScript 动画嵌入链路；MVP 离线三维可交互仿真交付统一收口至基于 Web 标准的「单文件离线 HTML 3D 动画包（Three.js + GLB 关键帧烘焙）」，2D 报告由已投产的 QPdfWriter 承载。若后续特定客户有强需求，直接转入商业 SDK（如 HOOPS Publish / Tech Soft 3D）技术选型评估，不在内部开展开源逆向或重度定制。至此，RISK-003 正式收口（CLOSED）。
- **触发阶段**：M14 报告导出与工程交付阶段。
- **验收条件**：已在设计文档与排期中明确以 HTML 3D 动画包保底交付，3D PDF 停止悬置并收口。

---

## UI-001 [RESOLVED in M09] 主工具栏“姿态平滑”按钮支持 `targetTable_` 多选局部区间插值（P2）

- **来源轮次**：`M08-TOOLPATH-EDIT-001` Round 1 审核（2026-10-26）。
- **位置**：`src/ui/ShellWindow.cpp` 姿态平滑动作与 `selectedTargetIdsFromTable`。
- **现状**：已在 M09（`M09-PROCESS-TUNE-001`）中闭环。选中行按行号排序，多选不少于 2 点时用首尾 `targetId` 做局部 SLERP，单选或未选退化为整条焊缝。
- **闭环验证**：Test 16 中区间内 `T_3` 插到约 20°，区间外 `T_1` / `T_5` 保持单位阵且 `!userModified`。M09 Round 1 审核已复核源码，维持 **RESOLVED**。

---

## M09-004 [RESOLVED in M10] 界面入口把未入栈的命令也报成成功（P2）

- **来源轮次**：`M09-PROCESS-TUNE-001` Round 1 审核（2026-10-26）。
- **位置**：`src/ui/MainWindow.cpp` 的 `smoothTargetOrientations`、`rotateTargetsTcpZ`、`modifyTargetProcessParams`。
- **现状**：已在 M10（`M10-001`）中闭环。`CommandManager::executeCommand` 在 `execute()` 之后按 `didMutate()` 返回 true/false。三处入口据此返回，零变化时不入栈且返回 false，界面状态栏提示无变化，不再显示成功。
- **闭环验证**：Test 17 验证零变化自转返回 false 且不入栈；`ShellWindow` 姿态平滑未发生变化时提示“未能执行姿态平滑：没有有效操作或中间点（无姿态变化）”。

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

---

## M10-008 [OPEN] 逆解失败后仍把默认构型当成已有解锁住（P2）

- **来源轮次**：`M10-COLLISION-REACH-001` Round 2 审核（2026-10-26）。
- **位置**：`MainWindow::scanSelectedTargetTwistAngles` 的 `hasPriorSolution`。
- **现状**：条件是 `cache.valid || status != Unchecked`。逆解失败时 `valid` 为 false，`solvedConfig` 保持默认 `N U T, 0, 0, 0`，状态却已经不是 `Unchecked`。扫描会锁住这套默认构型。已有成功解、后来只是把 `valid` 清掉的点不受影响，所以不挡 `M10-002`。
- **触发阶段**：下一次改自转扫描的构型锁，或要在不可达点上扫出其他构型的可行角时。
- **验收条件**：只有 cache 里确实写下过解（`valid`，或关节/构型来自一次成功的逆解）才锁定 `solvedConfig`。纯失败点不锁默认 `N U T`，参考关节用 Home。

---

## M10-009 [OPEN] 撤销落在未选中的另一段程序上时不会重算（P2）

- **来源轮次**：`M10-COLLISION-REACH-001` M10-007 复审（2026-10-26）。
- **位置**：`MainWindow::handleDocumentChanged`。
- **现状**：文档一变就重算当前选中操作所在的那一段程序。同一程序里的其它操作会一起更新。选中项已经换到另一段程序时，撤销只恢复被编辑点的快照，那段程序的下游 cache 和 `lastSimulationResult` 留在编辑后的解上。当前样例工位只有一段程序，不挡 M10 通过。
- **触发阶段**：一个工程里出现两段以上程序，并且撤销时选中项不在被修改的那段上。
- **验收条件**：撤销后，被修改的那段程序里每个点的 cache 与对该程序单独重算的结果一致；问题列表不保留该程序已过期的段内问题。

---

## M11-002 [OPEN] 插补问题停在计划对象里，界面和示例日志看不到（P2）

- **来源轮次**：`M11-TRAJECTORY-001` Round 1 审核（2026-10-27）。
- **位置**：`TrajectoryPlan::issues`；`SampleSession.cpp` 只打印 M10 的 `simRes.issues`；`TimelinePanel` 不读计划里的问题。
- **现状**：圆弧说明和直线 15° 问题已经写进 `TrajectoryPlan::issues`。`--verify` 仍只打印 M10 问题，退出码也只看 M10。这是本轮约定，不挡 `M11-001`。
- **后果**：操作者播放一条被插补器标过的直线时，问题列表可能仍是空的。
- **触发阶段**：问题列表要单独显示插补说明，或示例日志要带上这条说明时。
- **验收条件**：圆弧目标能在问题列表看到 `Circular motion is approximated as a straight line in this estimate`。直线关节步超过 15° 时能看到 `Linear segment joint step exceeds 15 degrees`。`--verify` 的退出码仍只由 M10 的阻塞问题决定，直到另有任务改这条约定。

---

## M11-003 [OPEN] 播放计划不随另一段程序的选中项重算（P2）

- **来源轮次**：`M11-TRAJECTORY-001` Round 1 审核（2026-10-27）。
- **位置**：`TimelinePanel::play`。`plan_` 已存在时直接接着播。
- **现状**：文档变更会 `invalidate()`。只改选中项不会。当前示例只有一段程序，不挡本轮。
- **后果**：工程里有两段程序时，选中另一段再按播放，时间轴仍播上一次的计划。
- **触发阶段**：一个工程里出现两段以上程序，并且播放前选中项换到另一段。
- **验收条件**：从新选中的操作所在程序播放时，帧和节拍对应该程序；上一段的 TCP 预览不留在视口里。

---

## M11-004 [OPEN] 直线采样超过 240 步后会放粗（P2）

- **来源轮次**：`M11-TRAJECTORY-001` Round 1 审核（2026-10-27）。
- **位置**：`TrajectorySimulator.cpp` 的 `kMaxLinearSamples`。
- **现状**：采样步目标是 5 mm / 2°，步数被夹在 2 到 240。大约 1.2 m 或 480° 以后，实际步长大于这个目标。15° 判断只看相邻采样。
- **后果**：更长的空走可能漏掉两个采样之间的关节尖峰。
- **触发阶段**：单段直线长于约 1.2 m，或姿态变化大于约 480°。
- **验收条件**：采样间距保持 5 mm / 2°；若仍要设上限，放粗后的相邻采样也不能放过超过 15° 的关节步。

---

## M11-005 [RESOLVED in M11-PLAYBACK-SHAPE-001 Round 2] 播放时连杆网格要跟着手臂姿态走（P2）

- **来源轮次**：`M11-TRAJECTORY-001` Round 1 审核（2026-10-27）。
- **位置**：`OcctViewport.cpp`、`TrajectorySimulator.cpp`、`TimelinePanel::stopPlayback`。
- **现状**：Round 2 已关闭。播放和静止姿态使用 `forwardRobotLinkWorldFrames`。停止不再发布起点帧。
- **闭环验证**：审核方复跑 Test 20。Home 连杆 4 与连杆 6 的位置、姿态断言为 PASS。`stopPlayback` 源码不再调用 `publish`。

---

## M11-009 [OPEN] 停止测试在没有计划时也会通过（P2）

- **来源轮次**：`M11-PLAYBACK-SHAPE-001` Round 2 审核（2026-10-28）。
- **位置**：`tests/test_simulation.cpp` 里对 `TimelinePanel::stopPlayback` 的钩子断言。
- **现状**：测试新建时间轴后直接 `stopPlayback()`，没有程序，`plan_` 为空。`publish` 在没有计划时立刻返回，不会调用 `showFrame`。把 `publish(false)` 加回停止函数，这条断言仍然通过。
- **后果**：`M11-008` 的回归可以被一次空停止掩盖。
- **触发阶段**：下次改 `stopPlayback` 或停止后的刷新时。
- **验收条件**：先建成一份计划并显示过一帧，再停止。`clearPreview` 被调用，`showFrame` 不被调用。

---

## M11-006 [OPEN] 时间轴没有按操作分项的节拍（P2）

- **来源轮次**：`M11-TRAJECTORY-001` Round 1 审核（2026-10-27）。
- **位置**：`TrajectoryPlan::operations`；`TimelinePanel::updateReadout`。
- **现状**：每个操作的焊接时间、空走时间和总时间已经累加，Test 18 核对过总和。时间轴读数只有总时间、焊接时间和空走时间，并写明节拍为估算。
- **后果**：多条焊缝时，界面看不出时间花在哪一条操作上。
- **触发阶段**：时间轴或后续报告要显示分操作节拍时。
- **验收条件**：界面能读到每个操作的焊接时间、空走时间和总时间，且各项之和等于周期时间。

---

## M12-004 [OPEN] 导出对话框不显示警告，且只覆盖第一段程序（P2）

- **来源轮次**：`M12-POST-FANUC-001` Round 1 审核（2026-10-27）。
- **位置**：`src/ui/LsExportDialog.cpp` 的 `refreshPreview`；`src/ui/MainWindow.cpp` 的 `findProgramInDoc`、`previewFanucLs`、`validateProgramForExport`。
- **现状**：校验结果 `ok == true` 时，状态行显示绿色通过，警告明细不列出。构型跳变因此在导出界面上不可见。勾选「尾部追加 Return Home」只追加 `! HOME joints deg` 注释；只有同时勾选「Home 输出为 GP1 关节点」才写出关节运动。取消前一个勾选后，后一个勾选不生效。对话框和空的程序号都固定取第一段程序。`exportAllPrograms` 没有菜单或工具栏入口。`previewFanucLs` 与 `validateProgramForExport` 不接收 `LsPostConfig`，碰撞阻断等选项只在对话框直接调用后处理器时生效。
- **后果**：两段以上程序时，操作者打开导出看到的可能是另一段程序。只含警告的程序会被显示成完全通过。
- **触发阶段**：一个工程里出现两段以上程序，或导出界面要列出非阻断警告、并提供整工位批量导出时。
- **验收条件**：预览和导出的是当前选中操作所在的那段程序。警告逐条显示，且不写成绿色通过。两个 Home 勾选的每种组合都与生成文本一致。批量导出有界面入口，或者界面明确只导出当前这一段。`previewFanucLs` 与 `validateProgramForExport` 使用同一份 `LsPostConfig`。

---

## M12-005 [RESOLVED in M12 Round 2] 不小于 1 mm/s 的直线速度向零截断（P2）

- **来源轮次**：`M12-POST-FANUC-001` Round 1 审核（2026-10-27）。
- **位置**：`src/io/FanucLsPostProcessor.cpp` 直线速度。
- **现状**：Round 2 改为 `std::round`。8.6 mm/s 写成 `9mm/sec`，与编程值之差小于 0.5 mm/s。Test 19 核对过这一条。`M12-001` 已关闭小于 1 mm/s 被改成 200 的路径。
- **闭环验证**：审核方复跑 Test 19 中 `Linear speed 8.6 mm/sec formats to nearest integer 9mm/sec` 为 PASS。

---

## M12-006 [OPEN] `.ls` 头部与控制器配置尚未按示教器样本收紧（P2）

- **来源轮次**：`M12-POST-FANUC-001` Round 1 审核（2026-10-27）。
- **位置**：`FanucLsPostProcessor::generateLsString` 的 `/ATTR` 与关节 `/POS`；`LsExportDialog` 的本地 `LsPostConfig`。
- **现状**：创建和修改时间固定为 `26-10-27 12:00:00`。`FILE_NAME` 为空，`PROG_SIZE` 为 0，`MEMORY_SIZE` 由行数估算。关节行写成 `J1 =`，常见示教器样本是 `J1=`。`TCD` 的三项写在同一行。预览是等宽暗色文本，没有语法高亮。导出选项只活在对话框里，不记在 `ControllerUnit` 上。无 MakeTP 时不能证明这些字段会被拒绝。加载对拍仍由 `RISK-001` 覆盖。
- **后果**：重新打开对话框后，组掩码和碰撞阻断回到默认值。头部字段与示教器导出的样本不一致。
- **触发阶段**：具备 ROBOGUIDE / MakeTP 做加载对拍时，或工程文件开始保存控制器配置时。
- **验收条件**：MakeTP 能编译本程序生成的 `.ls` 且无语法错误。关节位置行与该次对拍所用的示教器样本一致。每个控制器保留自己的组掩码、速度单位和碰撞阻断选项，重开导出对话框时仍是该控制器的那一份。

---

## M13-005 [OPEN] PDF 测试断言恒真，圆弧和多程序报告未核对（P2）

- **来源轮次**：`M13-REPORT-PDF-001` Round 1 审核（2026-10-27）。
- **位置**：`tests/test_report.cpp`；`WorkcellPdfReportExporter.cpp` 的运动类型与程序名循环。
- **现状**：Test 20 检查 `%PDF-`、文件大于 200 或 2000 字节，以及 `/Page` 出现至少两次。导出器总会 `newPage()` 一次，所以 0 个点也是两页。300 DPI 下第 2 页高 3224 像素、行高 26 像素，40 个点不会换页。空文档不检查有没有写成 38.51 秒或 UFRAME 原点。`MotionType::C` 走直线分支，画成 `L`。多段程序只保留最后一段名字，点号连成一张表。
- **后果**：坐标、节拍、裁切和换页退回归时，这组测试仍是绿的。
- **触发阶段**：下一轮改 Test 20，或报告要按程序分段时。`M13-001` ~ `M13-004` 要求的断言不依赖本条。
- **验收条件**：行高足以换页的点位数才会让页数增加；页数断言在点数低于这个阈值时失败。圆弧在后处理支持之前不画成 `L`。两段程序各自有程序名，点号不串成一段。

---

## M13-006 [OPEN] 焊缝清单、问题清单和每条焊缝的离屏多视角未进入本轮（P2）

- **来源轮次**：`M13-REPORT-PDF-001` Round 1 审核（2026-10-27）。
- **位置**：`Docs/OLP_System_Design_Claude.md` M13 的 2D 报告；`SimulationResult::issues`；`ShellWindow` 的单次 `grab()`。
- **现状**：设计基线里的 2D 报告还包括程序列表、焊缝编号/长度/条件号/速度、问题清单，以及每条焊缝起点、终点和全局多视角的离屏图。本轮菜单嵌入当前视口的一张图。示例的 `RETRACT` 扫掠间隙只写在仿真问题里，不在 `target.cache` 上，报告看不到。`PdfReportOptions::totalWeldLengthMm` 没有画出来。
- **后果**：工艺指导书还不能代替问题列表和焊缝表。3D PDF 已按 `RISK-003` 熔断收口（统一采用单文件离线 HTML 3D 动画包）。
- **触发阶段**：M13 继续做焊缝表、问题清单或离屏多视角时。单张视口图不把本条打回当前 P1。
- **验收条件**：每条焊缝有编号、长度、条件号和速度。问题清单能看到 `Sweep clearance below safe distance`。每条焊缝至少有一张离屏图，而不是只有操作者当前这一个视角。

---

## M13-008 [OPEN] 工具偏移和坐标系编号各有一份固定写法（P2）

- **来源轮次**：`M13-REPORT-PDF-001` Round 2 审核（2026-10-27）。
- **位置**：`src/ui/SampleSession.cpp` 的 `utoolOffset`；`WorkcellPdfReportExporter.cpp` 标定表第一列。
- **现状**：示例已经把 `tcpOffset` 写成 Z=150 mm 的工具节点，报告也读这个节点。同一函数里求解、过渡和轨迹仍使用另一份 `fromTranslation(0, 0, 150)`。标定表的编号固定是 `UTOOL[1]` 和 `UFRAME[1]`，不随程序上的编号变化。近关节限位、奇异和构型突变的状态文字不带间隙毫米数。
- **后果**：只改工具节点时，报告和求解会分开。程序使用 2 号工具时，表头仍写 1 号。
- **触发阶段**：工具偏移改由节点单独驱动，或程序的工具号、用户号不再是 1 时。
- **验收条件**：示例求解使用工具节点上的 `tcpOffset`，函数内不再保留第二份 150 mm。报告编号与该程序的 `utoolNumber`、`uframeNumber` 一致。

---

## M10-FCL-004 [RESOLVED in Round 2] 焊枪球碰到测试墙时向标准输出打调试行（P2）

- **来源轮次**：`M10-COLLISION-FCL-001` Round 1 审核（2026-10-28）。
- **位置**：`src/algo/FclCollisionQueries.cpp` 的 `queryRobotAndTorchCollision`。
- **现状**：Round 2 已去掉按 `Transit_Wall` 写 `std::cout` 的调试行。碰撞查询里不再按零件名字打印。
- **闭环验证**：审核方在 `src/` 中检索 `Transit_Wall` 与该调试打印，碰撞查询内已无此输出。

---

## M06-001 [RESOLVED in M11-PLAYBACK-SHAPE-001] 内置模型仍带一套字面量关节链（P2）

- **来源轮次**：M06-KINEMATICS-URDF-001 Round 1 审核（2026-10-28）。
- **位置**：RobotModel::createFanucM10iA；forwardUrdfFlange / forwardUrdfTool0。
- **现状**：已在 M11-PLAYBACK-SHAPE-001 中闭环。createFanucM10iA 不再填入字面量关节原点与转角；forwardUrdfFlange 与 forwardUrdfTool0 强制校验 if (!urdfLoaded) return ...;；loadFromUrdf 必须 6 个转动关节与 tool0 均解析成功才标记 urdfLoaded = true。
- **闭环验证**：审核方读了 `createFanucM10iA`、`forwardUrdfFlange`、`forwardUrdfTool0` 和 `loadFromUrdf`。未加载时返回单位变换。`urdfLoaded` 同时要求六个转动关节和 tool0。

---

## M06-002 [RESOLVED in M11-PLAYBACK-SHAPE-001] 金样在 URDF 打开失败后仍解引用空模型（P2）

- **来源轮次**：M06-KINEMATICS-URDF-001 Round 1 审核（2026-10-28）。
- **位置**：tests/test_kinematics.cpp 的 testOpwKinematics100Samples。
- **现状**：已在 M11-PLAYBACK-SHAPE-001 中闭环。在 testOpwKinematics100Samples 中，若 !urdfModelOpt.has_value() || !urdfModelOpt->urdfLoaded，记录一条失败后立即返回，不再解引用空 optional。
- **闭环验证**：审核方读了 Test 3。`urdfOk` 为 false 时 `check` 之后立刻 `return`。

---

## M10-FCL-005 [OPEN] 起弧内缩仍只看焊枪球（P2）

- **来源轮次**：`M10-COLLISION-FCL-001` Round 1 审核（2026-10-28）。
- **位置**：`CollisionAndSimEngine::measureOperationTorchClearance`；`SeamAndToolpathAlgo` 用它决定起点内缩。
- **现状**：这里调用 `checkTorchPoseCollision`，不传关节角，连杆 1～6 不参与。点位求解和扫掠已经查连杆。
- **后果**：内缩可以让焊枪球离开夹具，小臂仍可能碰到夹具。点位状态会在求解后另报碰撞。
- **触发阶段**：内缩或安全过渡要按整条手臂离夹具，而不只按焊枪球时。
- **验收条件**：同一条内缩用的间隙和点位求解用的间隙，都包含连杆 1～6 与四个焊枪球。

---

## M14-011 [RESOLVED in M14 Round 2] 采样帧轴向和零件网格没有被往返断言卡住（P2）

- **来源轮次**：`M14-PROJECT-JSON-001` Round 1 审核（2026-10-28）。
- **位置**：`tests/test_project_io.cpp` 的 M14-004、M14-006。
- **现状**：Round 2 已关闭。采样帧比较切向、两个面法向、角平分线和 `frame`。零件网格比较顶点数、三角形数和包围盒。
- **闭环验证**：审核方复跑这两条为 PASS。

---

## M14-012 [RESOLVED in M14-PROJECT-REBIND-002] 打开工程前不询问未保存修改（P2 -> CLOSED）

- **来源轮次**：`M14-PROJECT-JSON-001` Round 1 提出；`M14-PROJECT-REBIND-002` Round 1 审核确认（2026-10-31）。
- **位置**：`src/ui/ShellWindow.cpp:620-669`、`:636-642`。
- **现状**：脏文档打开或关闭时先 `confirmDiscardIfDirty`。取消不改变文档。确认后才打开。测试钩子存在时不弹窗。`LastTest.log:921-934` 为 PASS。
- **验收条件**：已满足。加载示例工位不在本条内，见 `M14-014`。

---

## M14-013 [RESOLVED in M14-PROJECT-REBIND-002] 打开工程不重建 CAD 形体，旧的 cadHandleId 可能指到别的形状（P2 -> CLOSED）

- **来源轮次**：`M14-PROJECT-JSON-001` Round 1 提出；`M14-PROJECT-REBIND-002` Round 1 审核确认（2026-10-31）。
- **位置**：`src/cad/OcctCadKernel.cpp:76-84`；`src/ui/MainWindow.cpp:784-844`；`src/ui/OcctViewport.cpp:394-403`。
- **现状**：打开成功后清空内核并重绑句柄。丢失或导入失败的节点句柄为 -1，网格保留。相对路径同文件只导入一次。`LastTest.log:935-948` 为 PASS。
- **验收条件**：已满足。非 ASCII 的 `sourceCadFile` 见 `M14-015`。

---

## M14-014 [OPEN] 加载示例工位不询问未保存修改（P2）

- **来源轮次**：`M14-PROJECT-REBIND-002` Round 1 审核（2026-10-31）。
- **位置**：`src/ui/ShellWindow.cpp:151-157`、`:586-614`；`src/ui/SampleSession.cpp:46-48`。
- **现状**：文件菜单「加载示例工位」直接调用 `loadSample`。`runSampleWorkcell` 在当前文档上执行 `ImportSceneAndReplaceRobotCommand`，不经 `confirmDiscardIfDirty`。导入成功会往现有场景上追加实体，并把文档标脏。
- **后果**：已有未保存修改时，这条菜单仍会改动当前工程，没有取消机会。
- **触发阶段**：下一轮改文件菜单或示例工位入口时。
- **验收条件**：文档为脏时，加载示例与打开、关闭使用同一条确认。取消后场景节点、程序和 `isDirty` 与点击前一致。不脏时不弹窗，示例仍能加载。无头测试用现有回调，不弹出 `QMessageBox`。

---

## M14-015 [OPEN] 重绑定 STEP 时把窄字符路径交给 importStep（P2）

- **来源轮次**：`M14-PROJECT-REBIND-002` Round 1 审核（2026-10-31）。
- **位置**：`src/ui/MainWindow.cpp:804-824`；`src/cad/OcctCadKernel.cpp:123-136`、`:730`。
- **现状**：存在性判断用 `utf8ToPath` 之后的 `filesystem::path`。真正导入调用 `importStep(resolvedPath.string())`。`importStep` 里的 `ifstream` 和 `ReadFile` 直接使用这串窄字符，不再走 `utf8ToPath`。本机示例路径是 ASCII，`LastTest.log` 的重绑定断言通过。`clear()` 在 `doc_` 替换之前执行；导入若抛异常，当前文档还在，内核已经空了。
- **后果**：`sourceCadFile` 含非 ASCII 且文件确实存在时，导入可能失败，句柄被写成 -1，视口退回网格。不会画上一个工程的形体。
- **触发阶段**：工程里的 `sourceCadFile` 含中文或其他非当前代码页字符时，或要让导入失败仍保持打开前的内核时。
- **验收条件**：存在的 `sourceCadFile` 用与 `utf8ToPath` 相同的路径对象完成导入，并写上新的 `cadHandleId`。导入失败或抛异常时，若新文档尚未替换，打开前的内核与句柄保持可用。ASCII 相对路径的现有重绑定断言仍通过。

## M15-001 [OPEN] T 型接头回归有几处断言盖不住交接里的几何和碰撞（P2）

- **来源轮次**：`M15-TJOINT-REGRESSION-001` Round 1 审核（2026-10-28）。
- **位置**：`tests/test_tjoint_regression.cpp` 的平分线循环、夹具名字核对，以及 `allTargetsValid`。
- **现状**：平分线偏差取了 `abs(dot)`，指向钢材内部的反向平分线也会得到 0°。两个外法向没有单独断言。点位只拒绝不可达、超限位和构型跳变；碰撞时 `cache.valid` 仍为 true，所以碰撞、近碰撞、奇异和近限位都会算通过。左右夹具只核对了名字，没有核对 `CollisionRole::Fixture`。弧长只比较首尾差 390 mm，没有要求逐帧递增。本轮实测有符号点积为 1，法向是 `(0, -1, 0)` 与 `(0, 0, 1)`，9 帧弧长从 5 mm 递增到 395 mm，12 个点都是 `Valid`，最小间隙 126.179 mm，两个夹具都是 `Fixture`。起弧切边停在初始 5 mm，这条前边没有再发生内缩。
- **后果**：标架翻进母材、夹具被标成工件，或求解后留下碰撞时，Test 23 仍可能是绿的。
- **触发阶段**：下次改 Test 23，或改焊缝标架、零件碰撞角色、点位状态时。
- **验收条件**：平分线与 `(0, -1, 1)` 的有符号夹角小于 0.01°。两个外法向分别贴近 `(0, -1, 0)` 和 `(0, 0, 1)`，误差小于 0.01°。左右夹具的 `collisionRole` 为 `Fixture`。采样弧长严格递增。每个目标点的状态为 `Valid`；碰撞、近碰撞、奇异和近限位都使该断言失败。


---

## FIX-EXPORT-P2 [RESOLVED in Round 2] 导出安全门禁 P2-A/B/C 收口（P2-D 另条）

- **来源轮次**：`FIX-EXPORT-SAFETY` 全盘复审；`MAINT-CLEANUP-P2-001` Round 2 审核确认（2026-10-31）。
- **位置**：`src/ui/MainWindow.cpp:554-570`、`:686-711`；`src/ui/SampleSession.cpp:231-232`；`tests/test_postprocessor.cpp:565-624`。
- **现状**：Round 2 审核核对源码与 `LastTest.log:638-641` 后关闭 A/B/C。
  1. **P2-A / MAINT-001**：`programCanonicalKey` 一段程序只记一个键。id 非空为 `id:`，不再用 name 做或匹配。同名 `WELD_001` 的 `prog_2` 在只 ensure `prog_1` 后仍被 const 校验和预览拒绝。
  2. **P2-B**：`exportAllPrograms` 对当前 `core::Program&` 调用 `ensureProgramSimulated`（`:701-702`）。
  3. **P2-C**：拒写时 `remove(olp::io::utf8ToPath(lsOut), ec)`，与 `exportToFile` 同一路径函数。见 `MAINT-003`。
  4. **P2-D 未关**：见 `FIX-EXPORT-P2-D`。
- **验收条件**：A/B/C 已满足。D 不在本条关闭范围内。

---

## FIX-EXPORT-P2-D [OPEN] 段内近碰撞盖掉端点奇异或近限位文案（P2）

- **来源轮次**：`FIX-EXPORT-SAFETY` 交叉审计 `OPEN-P2-1` / `P2-D`。`MAINT-CLEANUP-P2-001` Round 1 交接把它写进已关闭的伞形条目后删除。审核方拆回。本项不在该任务的 A/B/C 验收里，本轮不升为 P1。
- **位置**：`src/algo/CollisionAndSim.cpp:41-53`（`Singularity`、`NearJointLimit` 为 Soft，tier 0；`NearCollision` 为 tier 1）；`:37-65` `shouldUpgradeCacheStatus`；`:19-25` 段内写回 `cache.status`。
- **现状**：端点已是奇异或近限位时，段内近碰撞会升级 `cache.status` 并换掉 `issueMessage`。后处理里这两类都是 Warning，不改变拒写。`git diff` 对 `src/algo` 为空，本轮没有修。
- **后果**：点位上的奇异或近限位说明被近碰撞说明替换。导出门禁仍按 Warning 放行。
- **触发阶段**：下一轮改 `shouldUpgradeCacheStatus` 的 Soft/Near 分层，或要点位文案同时保留奇异与近碰撞时。
- **验收条件**：段内 `NearCollision` 不得擦掉端点 `Singularity` 或 `NearJointLimit` 的状态与说明；硬碰撞仍可覆盖它们。`OpwKinematics` 保持不改。

---

## MAINT-002 [OPEN] ensureProgramSimulated 恒为成功，批量导出的失败分支到不了（P2）

- **来源轮次**：`MAINT-CLEANUP-P2-001` Round 1 审核（2026-10-31）。
- **位置**：`src/ui/MainWindow.cpp:592-621` 与 `:705-715`。
- **现状**：找到程序后 `ensureProgramSimulated` 一律 `return true`（`:596`、`:620`）。仿真抛异常会直接冒泡，不会走进 `exportAllPrograms` 里 `"ensure failed"` 那条错误。没有碰撞几何时也先记账再返回 true。`M14-PROJECT-REBIND-002` 未改这条。
- **后果**：批量导出不会因为“这一段仿真没做完”单独记一条错误。当前写路径仍会对每一段调用仿真。
- **触发阶段**：下一轮改 `ensureProgramSimulated` 的返回值，或要让 `exportAllPrograms` 在单段仿真失败时跳过该段并继续其余程序时。
- **验收条件**：仿真没有跑完时返回 false，`:705-715` 能记到该程序的名字或 id。仿真跑完仍返回 true。无碰撞几何的现有单测保持通过。

---

## MAINT-003 [RESOLVED in Round 2] 拒写时删除旧 .ls 接入 utf8ToPath（P2 -> CLOSED）

- **来源轮次**：`MAINT-CLEANUP-P2-001` Round 1 提出；Round 2 审核确认（2026-10-31）。
- **位置**：`src/ui/SampleSession.cpp:10`、`:231-232`；`include/olp/io/Utf8Path.h:11`；`src/io/FanucLsPostProcessor.cpp:400`。
- **现状**：拒写分支调用 `std::filesystem::remove(olp::io::utf8ToPath(lsOut), ec)`，与 `exportToFile` 使用同一个 `olp::io::utf8ToPath`。`ec` 仍不检查。
- **验收条件**：路径函数已对齐。本条关闭。

---

## M13-HTML3D-003 [OPEN] 抽稀后的播放器保持上一关键帧，不插值（P2）

- **来源轮次**：`M13-HTML3D-EXPORT-001` Round 1 审核（2026-10-31）。
- **位置**：`src/io/HtmlAnimationExporter.cpp:319-330`（仅当相对弦的偏离小于容差才删帧），`:1007-1009` 与 `:1021-1031`（时钟和姿态用 `bakedFrames[curIdx]`，不在两帧之间插值）。
- **现状**：删除帧的前提是这段运动能由线性插值还原。播放端没有插值，姿态停在上一关键帧，到点再跳。本机 `WELD_001_Preview.html` 1047 帧抽成 184 帧；间隔 ≥ 0.2 s 的有 142 段，其中最大 TCP 跳动 2.43 mm、关节 0.14°。更快的共线空走可以整段跳过去。起弧/熄弧翻转仍保留了 2 次，不把本条升为 P1。
- **触发阶段**：下一轮改 HTML 播放器插值，或空走速度高到 0.25 s 内的直线跳变量不能接受时。
- **验收条件**：两个关键帧之间按时间对 TCP、六个关节和 7 个连杆矩阵插值。弦偏离仍受 `keyframePosTolMm` / `keyframeJointTolDeg` 约束。0 帧和 1 帧不除零。

---

## M13-HTML3D-004 [OPEN] 顶点哈希回绕、脚本结束符、旋转矩阵两位小数（P2）

- **来源轮次**：`M13-HTML3D-EXPORT-001` Round 1 审核（2026-10-31）。
- **位置**：`src/io/HtmlAnimationExporter.cpp:209-215`（`quantizeKey`），`:19-34`（`escapeJson`），`:433`（`std::setprecision(2)`）。
- **现状**：量化键只保留 21 位。大约 `[-100000, +109715]` mm 以内不碰撞；更远的坐标与负到超出偏置的坐标会并到同一顶点。`escapeJson` 处理了引号、反斜杠和换行，中文原样通过；零件名里的 `</script>` 会截断页面脚本。旋转矩阵元素打印成 0.01，小角度被收成 0。当前 T 型接头工位碰不到这三条。
- **触发阶段**：工位坐标超出约 ±100 m，或零件名/程序名要进入脚本字符串且含 `<`，或预览要保持亚度姿态时。
- **验收条件**：哈希在 ±1 km 内不回绕。脚本字符串里的 `<` 不会提前结束 `<script>`。连杆矩阵在 1° 转角下仍能看出非单位阵，或改为足够的小数位。

---

## M13-HTML3D-005 [OPEN] 第 8 节用子串代替几何断言（P2）

- **来源轮次**：`M13-HTML3D-EXPORT-001` Round 1 审核（2026-10-31）。
- **位置**：`tests/test_report.cpp:709-776`。
- **现状**：离线、网格、七节连杆、焊枪、相机和倍速都靠查找子串。`keyframeCount` 不是 21 即算抽稀成功，不要求起弧帧还在。枪尖偏离 TCP 约 212 mm 时，这 16 条仍然通过（本轮 821/0）。`M13-HTML3D-001` 要求的枪尖距离断言不依赖本条的其余子串检查。
- **触发阶段**：下一轮改 `tests/test_report.cpp` 的 HTML 段，或改抽稀/焊枪网格时。
- **验收条件**：Home 与一帧非零姿态下，枪尖到 `tcp` 的距离小于 1 mm。合成轨迹里弧状态切换的那一帧仍在导出的 `bakedFrames` 中。0 帧导出不抛异常，播放脚本不索引空数组。

---

## M13-HTML3D-006 [OPEN] HTML 导出与界面仿真的工具/用户坐标系取值不完全相同（P2）

- **来源轮次**：`M13-HTML3D-EXPORT-001` Round 1 审核（2026-10-31）。
- **位置**：`src/ui/MainWindow.cpp:735-768`（导出按工具号，优先 `tcpOffset`，`:750-751`）；`:354-358` 与 `src/ui/TimelinePanel.cpp:145-148`（第一个工具的 `localTransform`）；`src/ui/ShellWindow.cpp:202`（程序号传空串）；`src/io/HtmlAnimationExporter.cpp` 与 `src/app/CommandManager.cpp:129-135`（同号 UFRAME 一个取最后、一个取最前）。行号在 `MAINT-CLEANUP-P2-001` 插入导出函数后下移，原 `:706-740` 现为 `exportAllPrograms`。
- **现状**：未点仿真就导出时会重新 `plan`，不用过期的 `lastSimulationResult_`。示例里 `tcpOffset` 与 `localTransform` 都是 Z=150，只有一个工具、一个 UFRAME、一段程序，所以本轮 TCP 与界面一致。多工具、多程序或重复 UFRAME 编号时两边会分开。
- **触发阶段**：工程里出现 2 号工具、两段程序，或两个节点共用同一个 `uframeNumber` 时。
- **验收条件**：HTML 使用的基座、UFRAME、UTOOL 与时间轴播放同一段程序时用的是同一组变换。菜单导出当前选中程序；只有一段程序时行为与现在相同。
