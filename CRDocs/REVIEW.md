# 审核结论 — M13-REPORT-PDF-001

> **Task ID**：`M13-REPORT-PDF-001`
> **Task**：M13 2D PDF 工位工艺指导报告导出、节拍统计与 UI 联动
> **Round**：8
> **Reviewer**：审核方
> **Reviewed at**：2026-10-27
> **Executor**：执行方
> **Verdict**：**APPROVED**
>
> Round 8 复审。审核方复跑 `build-full/Release/olp_tests.exe`：**606 passed, 0 failed**，退出码 0。满量程姿态 `-180.0, -180.0, -180.0` 宽 726，姿态格宽 730。构型格宽 580，`F D B, -1, -1, -1` 宽 561。两格之间空 4 像素，和事件格也空 4 像素。坐标列 850、状态列 413、焊缝摘要 516 没有改小。`M13-001` 关闭。

M12 的 `M12-001` ~ `M12-003`、`M12-005` 维持 **RESOLVED**。`M12-004`、`M12-006` 仍在 `DEFERRED.md`。M11、M10 已关闭项不改写。

---

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M13-001 | P1 | **RESOLVED** | `src/ui/WorkcellPdfReportExporter.cpp`<br>`tests/test_report.cpp` | 满量程姿态 726 对 730。构型 561 对 580。坐标、焊缝摘要和状态列保持上一轮宽度。 |
| M13-007 | P1 | **RESOLVED** | `WorkcellPdfReportExporter.cpp`<br>`src/ui/ShellWindow.cpp` | 规划长度改为末帧弧长减首帧弧长。测试跨距 200 mm，旧公式 195 mm，两者已分开。 |
| M13-002 | P1 | **RESOLVED** | `WorkcellPdfReportExporter.cpp`<br>`src/ui/SampleSession.cpp` | 示例 UFRAME 为 (500, −300, 600)，UTOOL Z 为 150，底座 Z 为 500。空文档写「无程序 / 未标定」。 |
| M13-003 | P1 | **RESOLVED** | `WorkcellPdfReportExporter.cpp` | 节拍 ≤ 0 时为「未仿真」，占空比 0。不再写入 38.51 / 33.31。 |
| M13-004 | P1 | **RESOLVED** | `WorkcellPdfReportExporter.cpp` | 九种状态各有文字。未检查为灰色，不再把其它状态画成绿色「有效」。 |
| M13-005 | P2 | **DEFERRED** | `tests/test_report.cpp` | 圆弧写成直线。多程序只留最后一个名字。见 `DEFERRED.md`。 |
| M13-006 | P2 | **DEFERRED** | `WorkcellPdfReportExporter.cpp` | 焊缝清单、问题清单和每条焊缝的离屏多视角仍未做。见 `DEFERRED.md`。 |
| M13-008 | P2 | **DEFERRED** | `src/ui/SampleSession.cpp` | 求解仍使用另一份 150 mm 字面量。坐标系编号固定写成 `[1]`。见 `DEFERRED.md`。 |

---

### M13-001 [P1] RESOLVED

300 DPI、10 mm 页边距下，可绘区域是 2243×3272。审核方在绘制用的 `QPainter` 上重测。姿态格从 12 起、宽 730，满量程 `-180.0, -180.0, -180.0` 是 726。构型格从 746 起、宽 580，`F D B, -1, -1, -1` 是 561，表头「构型 CONFIG」是 297。事件格从 1330 起、宽 903，三个事件「起弧[1] 熄弧[1] 摆焊[1] 」是 594。姿态格结束于 742，构型格结束于 1326，中间和后面各空 4 像素。

坐标列仍是 850，`-9999.9, -9999.9, -9999.9` 是 825。焊缝摘要文本框仍是 516，`999/99999.9mm` 是 494。状态列仍是 413，「有效 (998.0mm)」是 396。表头和数据行用的是同一组常量，姿态走 `formatTargetOrientation`。Test 20 对满量程姿态、两处分界和构型串均为 PASS。

当前范围内的 P1 都已关闭。`M13-005`、`M13-006`、`M13-008` 仍在 `DEFERRED.md`，不挡本次通过。

### M13-002 [P1] RESOLVED

`extractMetadata` 从坐标系节点、工具节点和机器人节点读取位姿。示例用户坐标系原点是 (500, −300, 600) mm，工具 `tcpOffset` 的 Z 是 150 mm，底座 Z 是 500 mm。空文档的程序名为「无程序」，没有工具或坐标系时写「未标定」。导出正文使用这份元数据。Test 20 这几条均为 PASS。

### M13-003 [P1] RESOLVED

`cycleTimeSec <= 0` 时 `isSimulated` 为 false，三张时间卡写「未仿真」，占空比为 0。源码里已经没有 38.51 和 33.31。传入 40 s 与 30 s 时，空走为 10 s，占空比 75%。Test 20 这几条均为 PASS。

### M13-004 [P1] RESOLVED

九种状态都走到 `formatTargetStatusAndClearance` 和 `getTargetStatusColor`。未检查是灰色「未检查 (-)」，不打印默认 1000 mm。有效和近碰撞带实测毫米数。碰撞、不可达、超限为红色。近限位、奇异和构型突变为橙色。绘制点位行时使用这两个函数。Test 20 对九种文字和颜色均为 PASS。

### M13-007 [P1] RESOLVED

`ShellWindow` 和 `extractMetadata` 都改成末帧 `arcLengthMm` 减首帧 `arcLengthMm`。采样帧少于 2 个时长度为 0。Test 20 用起点 32 mm、末点 232 mm 的焊缝，跨距是 200.0 mm，旧的「末帧再减两端切边」是 195 mm。报告长度与跨距之差小于 0.1 mm，并且和旧公式相差超过 1 mm。这两条均为 PASS。

---

## 本轮已核对并保持

- 文件菜单、保存对话框、状态栏和失败弹窗仍接通。快照仍是当前视口一张图。
- 点位 XYZWR 来自 `poseInUFrame.toWPR()`。工艺条件来自 `weldSchedules`，速度用 `getWeldSpeedMmPerSec()`。构型字符串仍是 `N U T, 0, 0, 0` 这一格式。
- 条件表、坐标表和点位表在越过页脚时会 `newPage()` 并重画表头。三张表的行高是 84，子行绘制矩形高 36。第 1 页坐标表结束在约 1826 像素，页脚线在页高减 55，两者没有叠上。
- `ShellWindow.cpp` 980 行，`WorkcellPdfReportExporter.cpp` 705 行，`tests/test_report.cpp` 680 行，均低于 1000 行。
- 求解函数里仍有一份独立的 `fromTranslation(0, 0, 150)`。坐标系表第一列固定写 `UTOOL[1]`、`UFRAME[1]`。这两条记在 `M13-008`，不挡上面三项关闭。

---

# 审核结论 — M12-POST-FANUC-001

> **Task ID**：`M12-POST-FANUC-001`
> **Task**：M12 FANUC .ls 后处理器完备化、多操作/多坐标系切换、工艺指令映射、合规校验门禁与 UI 导出联动
> **Round**：2
> **Reviewer**：审核方
> **Reviewed at**：2026-10-27
> **Executor**：执行方
> **Verdict**：**APPROVED**（当前 `STATUS.md` 范围）
>
> Round 2 复审。审核方复跑 `build-full/Release/olp_tests.exe`：**482 passed, 0 failed**，退出码 0。复跑 `SiFastSuiteOLP.exe --verify`：退出码 0，1161 帧，38.5067 秒。`M12-001`、`M12-002`、`M12-003` 关闭。顺带落地的最近整数把 `M12-005` 一并关闭。

M11 的 `M11-001` 维持 **RESOLVED**。`M11-002` ~ `M11-006` 仍在 `DEFERRED.md`。M10 已关闭项不改写。

---

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M12-001 | P1 | **RESOLVED** | `src/io/FanucLsPostProcessor.cpp`<br>`tests/test_postprocessor.cpp` | 速度先取最近整数。关节不在 1..100、直线不足 1 mm/s、CNT 不在 0..100 时记错误并拒绝导出。 |
| M12-002 | P1 | **RESOLVED** | `src/io/FanucLsPostProcessor.cpp`<br>`tests/test_postprocessor.cpp` | 点名超过 16 字符、程序注释超过 32 字符记错误。运动行和 `/POS` 不再截断。 |
| M12-003 | P1 | **RESOLVED** | `src/io/FanucLsPostProcessor.cpp`<br>`tests/test_postprocessor.cpp` | 关节运动带 `WELD_SPEED` 记错误并拒绝导出。关节行只写百分比。 |
| M12-004 | P2 | **DEFERRED** | `src/ui/LsExportDialog.cpp`<br>`src/ui/MainWindow.cpp` | 警告在通过时不显示。Home 两个勾选与正文不一致。对话框只处理第一段程序。见 `DEFERRED.md`。 |
| M12-005 | P2 | **RESOLVED** | `src/io/FanucLsPostProcessor.cpp` | 8.6 mm/s 写成 `9mm/sec`。向零截断已去掉。 |
| M12-006 | P2 | **DEFERRED** | `src/io/FanucLsPostProcessor.cpp`<br>`src/ui/LsExportDialog.cpp` | 头部日期、`FILE_NAME`、`PROG_SIZE`、关节行空格和控制器配置尚未按示教器样本收紧。见 `DEFERRED.md`。 |

---

### M12-001 [P1] RESOLVED

校验和生成都先 `std::round`。关节速度不在 1..100、非焊接直线速度不足 1 mm/s、CNT 不在 0..100 时记错误。`generateLsString` 在校验失败时返回空字符串，正文里不再把 0 改成 50% 或 200 mm/s，也不再把 CNT 夹到 100。

0.4% 和 0.4 mm/s 四舍五入后是 0，导出被拒绝。150% 和 CNT 250 同样被拒绝。8.6 mm/s 写成 `9mm/sec`。关节行在校验通过后只写这个整数百分比。Test 19 这几条均为 PASS。

### M12-002 [P1] RESOLVED

点名超过 16 字符、程序注释超过 32 字符记错误，`generateLsString` 返回空字符串。运动行和 `/POS` 直接写 `t.name`，没有 `substr`。`PROGRAM_NAME_16C` 与 `TARGET_NAME_16CH` 都是 16 个字符，对应的 17 字符样本被拒绝。程序注释用 32 个 `C` 通过、33 个 `C` 拒绝。Test 19 先断言长度再断言结果，这几条均为 PASS。

### M12-003 [P1] RESOLVED

`motion == J && useWeldSpeed` 记错误，生成函数在写运动行之前返回。关节分支只写百分比，`WELD_SPEED` 只出现在非关节运动上。Test 19 对这个组合断言校验失败且正文为空，结果为 PASS。

---

## 本轮已核对并保持

- 程序名规则与交接一致：大写字母开头，仅含大写字母、数字和下划线，长度 1..16。空程序、逆解无效、圆弧运动 `C` 会拒绝导出，不会把圆弧写成直线。
- 示例程序 `build/WELD_001.ls` 的段头、`UFRAME_NUM`/`UTOOL_NUM`、`Arc Start[1]`/`Arc End[1]`、`WELD_SPEED` 和 XYZWR 三位小数与现有 `toWPR()` 一致。配置字来自 `FanucConfig::toString()`，格式为 `N U T, 0, 0, 0`。
- `startState.homeJointsRad` 与机器人模型的 Home 同为示教器显示角。默认 Home 的 J2 为 0，按现有耦合式写出的 J3 与显示角相同。本条不重开 `RISK-001`。
- 多操作点号连续，`exportControllerPrograms` 写出两份文件。工具栏「导出」和文件菜单都会打开 `LsExportDialog`。`ShellWindow.cpp` 按编辑器行号为 948 行，交接里的 880 是非空行计数。各改动文件均低于 1000 行。
- 默认导出的回 Home 仍是注释行。勾选「Home 输出为 GP1 关节点」且同时保留回 Home 时，才会追加 `J P[n:HOME]` 和关节 `/POS`。该勾选组合的界面问题记在 `M12-004`，不挡上述语法。

---

# 审核结论 — M11-TRAJECTORY-001

> **Task ID**：`M11-TRAJECTORY-001`
> **Task**：M11 轨迹插补、节拍估算、帧烘焙与时间轴回放
> **Round**：3
> **Reviewer**：审核方
> **Reviewed at**：2026-10-27
> **Executor**：执行方
> **Verdict**：**APPROVED**（当前 `STATUS.md` 范围）
>
> Round 3 复审。审核方复跑 `build-full/Release/olp_tests.exe`：**425 passed, 0 failed**，退出码 0。复跑 `SiFastSuiteOLP.exe --verify`：退出码 0，1161 帧，38.5067 秒（焊接 33.305 秒，空走 5.20174 秒）。`CollisionAndSim.cpp` 与 `OpwKinematics.cpp` 无差异。`M11-001` 关闭。

M10 的 `M10-001` ~ `M10-003`、`M10-007` 维持 **RESOLVED**。`M10-004` ~ `M10-006`、`M10-008`、`M10-009` 仍在 `DEFERRED.md`。本轮不改写下方 M10 结论。

---

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M11-001 | P1 | **RESOLVED** | `src/algo/simulation/TrajectorySimulator.cpp`<br>`tests/test_simulation.cpp` | 直线采样、15° 判断、限速和播放共用短程增量。+180° 圈数对不上时，在同一手臂姿态里按短程增量选解。 |
| M11-002 | P2 | **DEFERRED** | `TrajectoryPlan::issues`<br>`src/ui/SampleSession.cpp`<br>`src/ui/TimelinePanel.cpp` | 圆弧说明和 15° 问题写在计划里。示例日志和问题列表仍只显示 M10。见 `DEFERRED.md`。 |
| M11-003 | P2 | **DEFERRED** | `src/ui/TimelinePanel.cpp` | 已有播放计划时，选中另一段程序不会重算。见 `DEFERRED.md`。 |
| M11-004 | P2 | **DEFERRED** | `TrajectorySimulator.cpp` | 直线采样上限 240，长路径会粗于 5 mm / 2°。见 `DEFERRED.md`。 |
| M11-005 | P2 | **DEFERRED** | `src/ui/OcctViewportPlayback.cpp` | 播放只画 TCP 球和轨迹，连杆网格不随帧摆动。见 `DEFERRED.md`。 |
| M11-006 | P2 | **DEFERRED** | `src/ui/TimelinePanel.cpp` | 按操作分项的时间已算进计划，时间轴只显示总时间、焊接时间和空走时间。见 `DEFERRED.md`。 |

---

### M11-001 [P1] RESOLVED

直线采样用 `shortestJointStep` 把关节折成相对上一采样的短程角。15° 判断、梯形限速和 `jointsAt` 读这组增量。段末用同一次判断对齐终点。关节运动仍写入终点主值的原始差值。

锁定圈数找不到解时，`samePostureByShortStep` 在翻转、肘、肩相同且未超限位的候选里，按短程增量的平方和选取，然后再做 15° 判断。超过 15° 时问题句仍是 `Linear segment joint step exceeds 15 degrees`。

Test 18 覆盖 J6 从 179° 到 −179°、从 −179° 到 +179°，以及 J4 从 −170° 到 +170°、从 +170° 到 −170°。后三条断言不报无逆解、不置 `hasConfigJumpInLinear`、相邻关节步小于 15°，每个采样的正运动学落在该点球面插值上（0.5 mm、1° 以内）。审核方复跑这三项均为 PASS。Round 2 里 J4 负侧折点停在 −178°、偏差 3.47 mm 的路径已经消失。

---

## 本轮已核对并保持

- 关节运动六轴共用一条梯形路径参数，同时起停。50% 速度比 100% 更慢。Test 18 的梯形数值与 `makeTrapezoid` 一致。
- 直线运动在采样点上做位置线性插值和姿态球面插值，逆解锁终点构型。焊接段的构型锁与 M10 相同：起点到达后锁定，熄弧后放开。
- 起弧、熄弧在到达之后生效。焊接时间只计已经起弧的那段运动。总时间等于焊接加空走，并按操作累加。
- 烘焙含 0、约 30 FPS 网格、总时长和各段终点。帧上的连杆变换与正运动学一致。
- 播放、暂停、停止、按点、步进 0.1 秒、倍速、拖动时先暂停、从选中点起播，都接到 `PlaybackCursor`。未到结尾时播放不倒回；到结尾再播才回到程序起始关节。停止回到 0。倍速夹在 0.1 到 8。
- 播放只改视口里的 TCP 球和轨迹，点动读数写「仿真（未写入程序）」。不写机器人节点，不入命令栈。文档刷新会 `invalidate()` 并清掉预览。
- 圆弧记一条 `Circular motion is approximated as a straight line in this estimate`，然后按直线估算。拐角各自停稳，没有混合。
- 示例会话仍用 `validateAndSimulateProgram` 决定问题和退出码，再用 M11 的帧和三项时间覆盖导出。
- `ShellWindow.cpp` 935 行。新的仿真、时间轴、视口预览和 Test 18 各自成文件，都低于 1000 行。

---

# 审核结论 — M10-COLLISION-REACH-001

> **Task ID**：`M10-COLLISION-REACH-001`
> **Task**：M10 可达性、奇异点、碰撞与自转角扫描 — M10-007 复审
> **Round**：2
> **Reviewer**：审核方
> **Reviewed at**：2026-10-26
> **Executor**：Gemini / Claude
> **Verdict**：**APPROVED**（当前 `STATUS.md` 范围）
>
> 对照提交 `70bd8f8`。审核方复跑 `build-full/Release/olp_tests.exe`：**368 passed, 0 failed**。`M10-001` ~ `M10-003` 维持 **RESOLVED**。`M10-007` 关闭。

M09 的 `M09-001` ~ `M09-003` 维持 **RESOLVED**。`M10-004` ~ `M10-006`、`M10-008` 仍在 `DEFERRED.md`。本结论不覆盖这些延期项，也不表示 M11 已开始。

---

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M10-001 | P1 | **RESOLVED** | `src/ui/MainWindow.cpp`<br>`src/app/CommandManager.cpp` | 入栈之后才返回是否变更。自转快照含整份 cache。选中点的关节会写回机器人节点。 |
| M10-002 | P1 | **RESOLVED** | `src/ui/MainWindow.cpp`<br>`src/algo/CollisionAndSim.cpp` | 已有解时锁定 `cache.solvedConfig`。焊接角色豁免焊丝尖端，接近点不豁免。 |
| M10-003 | P1 | **RESOLVED** | `src/ui/ShellWindow.cpp` | 问题列表含段内扫掠与构型跳变，条目带 `targetId`、`operationId`。 |
| M10-007 | P1 | **RESOLVED** | `src/ui/MainWindow.cpp`<br>`src/ui/ShellWindow.cpp` | 撤销、重做和入栈都会重算当前程序的全部点 cache 与 `lastSimulationResult`。已删除的点和操作不再留在问题列表里。 |

---

### M10-007 [P1] RESOLVED

`MainWindow` 在构造时注册文档监听。`execute`、`undo`、`redo` 入栈或弹出之后都会 `notifyDocumentChanged()`。监听里的 `handleDocumentChanged()` 对当前操作所在的整段程序调用 `validateOperation`，写回每个点的关节、构型、状态、间隙和问题文本，并替换 `lastSimulationResult_`。`inValidation_` 挡住这次验证再进入监听。`validateOperation` 本身不再发文档通知，不会套娃。

监听注册在界面刷新之前。同一轮通知里先重算，`ShellWindow` 再 `fillIssues()`。撤销后的列表读到的是恢复姿态上的那次仿真。

选中操作已被删掉时，选择和 `lastSimulationResult_` 被清空；文档里不再有操作时也不再保留旧问题。`fillIssues()` 对找不到的操作或目标点直接跳过。

Test 17 在夹具仍在场时记录下游点 2、点 3 的关节、构型和 `issues` 文本，再把起点自转 20°。撤销后这三项与自转前的记录一致，重做后与自转后的记录一致。继续撤销到生成操作消失后，`issues` 为空，问题列表里没有已删除点的 `targetId`。

被编辑点自己的姿态和关节仍由命令快照带回，监听只是按恢复后的姿态再解一次。这次重解与编辑前那次验证一致，所以 `M10-001` 的撤销对称仍然成立。
