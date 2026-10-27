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
