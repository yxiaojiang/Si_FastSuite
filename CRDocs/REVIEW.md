# 审核结论 — M09-PROCESS-TUNE-001

> **Task ID**：`M09-PROCESS-TUNE-001`
> **Task**：M09 工艺事件与路径微调 — Round 2 复审
> **Round**：2
> **Reviewer**：审核方
> **Reviewed at**：2026-10-26
> **Executor**：Gemini / Claude
> **Verdict**：**APPROVED**（Round 1 的 `M09-001` ~ `M09-003` 已闭环）

审核对照源码、调用链和回归断言，不以 `IMPLEMENTATION.md` 自述为完成证据。审核方复跑 `build-full/Release/olp_tests.exe`：**298 passed, 0 failed**。

---

## 复审结论

| ID | 级别 | 状态 | 涉及文件 | 核对依据 |
|---|---|---|---|---|
| M09-001 | P1 | **RESOLVED** | `src/ui/ShellWindow.cpp`<br>`tests/test_main.cpp` | `parseSpeedCellText` 去掉 `WELD_SPEED`、括号和单位后再取数。`WELD_SPEED (80 mm/s)` 得到 `useWeldSpeed=true`、80 mm/s；`80 mm/s` 与裸数字退出 `WELD_SPEED`；`50%` 为 50；`50 cm/min` 按 \(\times 10/60\) 得到 \(8.333\) mm/s。`formatSpeedCellText` 与解析对 `WELD_SPEED (80 mm/s)`、`150 mm/s`、`60%` 往返一致。Test 16 上述断言全部通过。 |
| M09-002 | P1 | **RESOLVED** | `src/ui/ShellWindow.cpp`<br>`include/olp/ui/ShellWindow.h` | `itemChanged` 在调用命令期间把 `inItemChanged_` 置位。文档监听器这时只排 `QTimer::singleShot(0)`，等信号返回后再 `refreshFromDocument`。非法输入同样延后刷新。`RefreshGuard` 用 `refreshDepth_` 嵌套计数，`rebuildTargetTable` 不再把外层守卫提前清掉。刷新过程中 `isRefreshing()` 挡住 `itemChanged` 重入。 |
| M09-003 | P1 | **RESOLVED** | `include/olp/core/ProjectModel.h`<br>`src/algo/SeamAndToolpathAlgo.cpp`<br>`src/app/CommandManager.cpp`<br>`tests/test_main.cpp` | `processModified` 与 `userModified` 分开。工艺命令置位并在撤销时恢复。`generateOperationTargets` 只在 `processModified` 时按弧长/角色恢复运动、速度、`useWeldSpeed`、终止类型和事件；仅姿态手改仍重写工艺字段。Test 16：Schedule 3 与 CNT 80 / 速度 85 / 运动 J 在重生成后仍在；只自转 30° 的点保持 `userModified`，CNT 回到 50，运动回到 L。 |

Round 1 已通过的默认工艺规则、绕 TCP Z 自转不变量、局部 SLERP（UI-001）和工艺命令对称性，本轮未回退。M08 的 `M08-003` ~ `M08-006` 维持 **RESOLVED**。

---

## 本轮新登记的 P2

`M09-008`、`M09-009` 已写入 `DEFERRED.md`。不挡本次通过。`M09-004` ~ `M09-007` 仍延期。
