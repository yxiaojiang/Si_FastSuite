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
