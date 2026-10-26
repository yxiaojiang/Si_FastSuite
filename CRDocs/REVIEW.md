# 审核结论 — M10-COLLISION-REACH-001

> **Task ID**：`M10-COLLISION-REACH-001`
> **Task**：M10 可达性、奇异点、碰撞与自转角扫描 — Round 2
> **Round**：2
> **Reviewer**：审核方
> **Reviewed at**：2026-10-26
> **Executor**：Gemini / Claude
> **Verdict**：**CHANGES_REQUESTED**
>
> 对照提交 `3968211` 的源码和调用链。审核方复跑 `build-full/Release/olp_tests.exe`：**357 passed, 0 failed**。`M10-001` ~ `M10-003` 的原验收已经落到代码和 Test 17 上。撤销后的仿真结果没有跟着回去，问题列表会继续显示已经撤销的段内问题。

M09 的 `M09-001` ~ `M09-003` 维持 **RESOLVED**。

---

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M10-001 | P1 | **RESOLVED** | `src/ui/MainWindow.cpp`<br>`src/app/CommandManager.cpp` | `executeCommand` 在 `execute()` 之后返回是否入栈。三条入口据此重新验证，并把选中点的关节写到机器人节点。自转快照保存整份 `TargetCache`，撤销/重做能带回该点的姿态和关节。 |
| M10-002 | P1 | **RESOLVED** | `src/ui/MainWindow.cpp`<br>`src/algo/CollisionAndSim.cpp` | 已有解时锁定 `cache.solvedConfig` 和 `cache.jointsRad`。焊接角色豁免焊丝尖端，`APPROACH` 在同一几何上判碰撞。 |
| M10-003 | P1 | **RESOLVED** | `src/ui/ShellWindow.cpp` | `fillIssues()` 读入 `lastSimulationResult.issues`，扫掠碰撞能出现在列表里，并带上 `targetId` 与 `operationId`。 |
| M10-007 | P1 | **OPEN** | `src/ui/MainWindow.cpp`<br>`src/app/CommandManager.cpp`<br>`src/ui/ShellWindow.cpp` | 验证写了整条程序的 cache 和 `lastSimulationResult`，撤销只恢复被编辑的点。问题列表和下游点的解留在编辑后的姿态上。 |

`M10-004` ~ `M10-006` 仍延期。`M10-008` 已写入 `DEFERRED.md`。

---

### M10-001 [P1] RESOLVED

`CommandManager::executeCommand` 先 `execute()`，再按 `didMutate()` 返回 true/false。`smoothTargetOrientations`、`rotateTargetsTcpZ`、`modifyTargetsProcessParams` 用这个返回值决定是否 `validateOperation`。零增量自转在入栈前返回 false。

`MainWindow` 在文档变更时调用 `syncRobotJointsFromSelectedTarget()`。选中点 `cache.valid` 时，机器人节点的 `currentJointsRad` 换成该点的解。Test 17：15° 自转返回 true，撤销后该点姿态、`userModified`、关节与机器人节点回到自转前，重做后再对齐。

自转和工艺命令的快照改为整份 `TargetCache`。第一次 `undo()` 把验证后的 cache 封进 `afterSnapshots_`，重做时按这份快照写回。被编辑的那一个点，撤销/重做是对称的。

---

### M10-002 [P1] RESOLVED

`scanTwistAngles` 的构型参数改为 `optional<FanucConfig>`，并增加 `isWeldingRole`。`scanSelectedTargetTwistAngles` 在 `cache.valid` 或状态已不是 `Unchecked` 时锁定 `cache.solvedConfig`，参考关节用 `cache.jointsRad`。角色只有 `WELD_START` / `WELD_VIA` / `WELD_END` 才把 `isWeldingRole` 设为 true。

Test 17 经 `MainWindow::scanSelectedTargetTwistAngles`：当前 0° 可达；母材顶面贴在焊缝上时，焊接角色 `collisionFree`，同一姿态改成 `APPROACH` 后不豁免。失败解仍会锁住默认构型的缺口记在 `M10-008`，不挡本条关闭。

---

### M10-003 [P1] RESOLVED

`fillIssues()` 先写入 `lastSimulationResult().issues`，再用 `{targetId, description}` 与点缓存去重。硬碰撞、不可达、超限、构型跳变为红色，其余警告为橙色。Test 17 在两端点之间放入 `Fixture_Clamp_Middle`，仿真结果和 `ShellWindow` 的问题列表都能看到该夹具，条目带有 `targetId` 与 `operationId`。单击/双击仍走 `selectOperation` + `selectTarget`。

列表的内容在撤销后会过期，见 `M10-007`。显示路径本身已经接上。

---

### M10-007 [P1] OPEN — 撤销后仿真结果和下游 cache 仍是编辑后的

`validateAndSimulateProgram` 会改写该程序里每一个点的 cache，并把段内扫掠、直线段无解和构型跳变放进 `SimulationResult`。`MainWindow` 把这份结果留在 `lastSimulationResult_`。这次写入发生在 `executeCommand` 返回之后，不在命令快照里。

`CommandManager::undo()` 只调用命令自己的 `undo()`，然后 `notifyDocumentChanged()`。自转命令的快照只有被转的那些点。下游点的关节、构型和问题文本保持验证后的值。`lastSimulationResult_` 完全不恢复，也不重算。

`fillIssues()` 先画 `lastSimulationResult_.issues`。因此：

- 自转引入一段扫掠碰撞后再撤销，姿态已经回去，问题列表仍显示这段碰撞。
- 自转消掉一段碰撞后再撤销，列表上的段内条目不会回来。
- 焊接段的构型锁记在 `WELD_START` 的解上。这次自转若换成另一套构型，下游点的 cache 留在新构型里，撤销后的 `WELD_START` 却是旧构型。导出时一条焊缝里会混两套构型。
- 撤销“由焊缝生成操作”后，操作已经不在文档里，`lastSimulationResult_` 里的旧 `targetId` 仍会进问题列表。

重做刚好把被编辑点放回验证后的快照，而 `lastSimulationResult_` 一直停在那次验证上，所以重做之后列表又看起来是对的。断的是撤销这一侧。

Test 17 只比较了被转那一个点的关节，没有读下游点，也没有在撤销之后看问题列表。

**验收**

1. 自转、平滑或工艺修改触发验证之后，再撤销一次：该程序每个点的 cache（关节、构型、状态、间隙、问题文本）以及 `lastSimulationResult.issues` 与撤销后的姿态一致。与“编辑前先验证一次、再比较”的结果相同即算一致。
2. 重做后，上述 cache 和段内问题回到编辑后的那次验证。
3. 撤销生成操作后，问题列表不再出现已删除点的条目。
4. Test 17 记录自转前下游点的关节和 `issues` 文本，撤销后逐项相等；重做后再与自转后的记录相等。不要只断言被转的那一个点。
