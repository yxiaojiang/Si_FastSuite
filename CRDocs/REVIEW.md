# 审核结论 — M08-TOOLPATH-EDIT-001

> **Task ID**：`M08-TOOLPATH-EDIT-001`
> **Task**：M08 路径生成与编辑（焊缝到操作路径生成、切入切出段配置、多点姿态 SLERP 插值与程序树/路径表联动导航）— Round 1 审核
> **Round**：1
> **Reviewer**：Argon（审核方）
> **Reviewed at**：2026-10-26
> **Executor**：Gemini / Claude
> **Verdict**：**CHANGES_REQUESTED**（自动化测试 163/163 通过、`--verify` 端到端通过，但存在 4 项 P1 级命令对称性/跨操作选择断链问题需在 Round 2 收敛）

---

## 审核模板说明

复审对象为工作区中与本模块相关的代码与文档文件。审核者必须检查实际代码、数学几何边界、命令对称性、diff 和调用链，不以 `IMPLEMENTATION.md` 自述作为完成证据。

问题分类等级：
- **P0**：阻断级（编译失败、核心几何算法崩溃、不可逆破坏、数据模型损坏）。
- **P1**：当前阶段必须修复（精度不达标、命令对称性丢失、漏选/漏更新、交互断链）。
- **P2**：可延期但必须登记（次要 UI 瑕疵、极端容错、未来特性演进），统一移入 `DEFERRED.md`。

---

## 既有审核状态（M07 边链与动态曲面焊缝）

M07（切线连续边链扩展、动态曲面母材外法向投影、复合曲率焊缝生成与闭合环识别）已于提交 `95807b6` 完成独立审计并完全闭环（通过 130 项自动化验证与端到端校验，状态为 **APPROVED**）。

---

## 本轮审核结论清单（M08 Round 1）

| ID | 级别 | 状态 | 涉及文件 | 审核结论与修复要求 |
|---|---|---|---|---|
| M08-001 | P1 | **RESOLVED** | `src/algo/SeamAndToolpathAlgo.cpp` | `endpointsOnlyForLine` 直线共线（弦高偏差 $\le 0.05\text{ mm}$）与切线一致性（点积 $\ge 0.98$）判定严谨，圆环与多段链完整保留 `WELD_VIA_*` 序列。 |
| M08-002 | P1 | **RESOLVED** | `include/olp/core/ProjectModel.h`<br>`src/algo/SeamAndToolpathAlgo.cpp` | `APPROACH` / `RETRACT` 准确沿焊枪 $-Z$ 轴按 `approachDistMm` / `retractDistMm` 退避，支持 `approachMotion`、`retractMotion`、`retractSpeedMmPerSec` 独立配置。 |
| M08-003 | P1 | **OPEN** | `src/algo/SeamAndToolpathAlgo.cpp` | 四元数与矩阵 SLERP 数学精度达标（中点误差 $< 10^{-6\circ}$），但 `interpolateOrientationsSlerp`（第 652–674 行）在起止点未修改或插值后姿态未发生实质变化时，仍无条件将所有中间点置为 `userModified = true`、清空 `cache.valid` 并返回 `true` 压栈，违反无变更命令不入栈门禁。详见下方 `M08-003`。 |
| M08-004 | P1 | **OPEN** | `src/app/CommandManager.cpp` | 1. `RegenerateOperationTargetsCommand::execute()` 置 `seam->programmed = true`，但 `undo()` 未恢复 `beforeSeamProgrammed_`；<br>2. `GenerateOperationFromSeamCommand` 在控制器已存在但 `programs` 为空时自动创建 `prog_1`，`undo()` 未回滚新建的 `prog_1`，且未拦截空 `sampledFrames`。详见下方 `M08-004`。 |
| M08-005 | P1 | **OPEN** | `src/ui/MainWindow.cpp`<br>`src/ui/ShellWindow.cpp` | 1. `MainWindow::selectTarget` 在省略 `operationId` 且 `selectedOperationId_` 已非空时，第 47 行 `if (!selectedOperationId_.empty()) break;` 在检查完第一个操作后即提前中断外层循环，无法定位后续操作中的目标点；<br>2. `ShellWindow::applySelection` 在选中目标点切换所属操作时提前 `return`，未触发 `rebuildTargetTable()` 刷新表格。详见下方 `M08-005`。 |
| M08-006 | P1 | **OPEN** | `tests/test_main.cpp` | 现有 163 项断言全部通过，需在 `Test 15` 补充针对 `M08-003`（零变化 SLERP 不入栈）、`M08-004`（重算命令撤销恢复 `programmed`）与 `M08-005`（跨操作 `selectTarget` 自动反查操作 ID）的回归断言。 |

---

## OPEN 问题详情（Round 2 必改项）

### M08-003 [OPEN] `interpolateOrientationsSlerp` 零姿态变化时仍污染 `userModified` 与命令栈（P1）
- **位置**：`src/algo/SeamAndToolpathAlgo.cpp:564-675`
- **现象**：
  - 当首尾目标点未做旋转修改（$\Delta R_A = \Delta R_B = I$）或中间点当前旋转 `t.poseInUFrame.rot` 与插值结果 `newRot = baseRot * interpolatedDelta` 夹角为 $0^\circ$ 时，第 667–671 行仍无条件写入 `t.userModified = true; t.cache.valid = false; updatedAny = true;`。
  - 这导致用户在未旋转首尾点的情况下点击“姿态平滑”，会向 Undo 栈压入一条无效命令，并把所有 `WELD_VIA` 点锁定为 `userModified = true`。
  - 同时，形参 `seam` 与 `uframeInWorld` 在函数内未使用。
- **修复要求**：
  - 计算 `const core::Mat3 newRot = baseRot * interpolatedDelta;`，仅当 `t.poseInUFrame.rot.angularDistanceRad(newRot) > 1e-6` 时才修改该目标点的 `poseInUFrame.rot`、`userDeltaOffset.rot`、`userModified`、`cache.valid` 并置 `updatedAny = true`。
  - 清理或 `(void)` 显式标记未使用的 `seam`、`uframeInWorld` 形参。

### M08-004 [OPEN] 路径生成与重算命令的 Undo 对称性漏洞（P1）
- **位置**：`include/olp/app/CommandManager.h:136-156`、`src/app/CommandManager.cpp:318-513`
- **现象**：
  1. `RegenerateOperationTargetsCommand::execute()`（第 501 行）修改了 `seam->programmed = true;`，但其 `undo()`（第 506–513 行）未保存并恢复 `seam->programmed = beforeSeamProgrammed_;`。
  2. `GenerateOperationFromSeamCommand::execute()`（第 355–367 行）在 `doc_.controllers` 非空但 `doc_.controllers[0].programs` 为空时新建了 `prog_1`，但未记录 `createdProgram_`，导致 `undo()` 时残留空 `prog_1`；此外当 `seam->sampledFrames.empty()` 时未拒绝执行。
- **修复要求**：
  - 在 `GenerateOperationFromSeamCommand` 中：若 `seam->sampledFrames.empty()` 则置 `mutated_ = false; return;`；新增 `bool createdProgram_{false};` 并在 `undo()` 中对称弹出自动创建的 `prog_1`。
  - 在 `RegenerateOperationTargetsCommand` 中：保存 `beforeSeamProgrammed_` 并在 `undo()` 中恢复。

### M08-005 [OPEN] `MainWindow::selectTarget` 跨操作反查提前中断及 `ShellWindow` 路径表漏刷新（P1）
- **位置**：`src/ui/MainWindow.cpp:30-53`、`src/ui/ShellWindow.cpp:919-975`
- **现象**：
  1. `MainWindow::selectTarget(const std::string& targetId, const std::string& operationId)` 中，当 `operationId` 为默认空字符串且 `selectedOperationId_` 原本非空（例如 `"op_1"`）时，第 47 行 `if (!selectedOperationId_.empty()) break;` 在遍历完第一个操作 `"op_1"` 后因旧值非空而直接跳出三层循环，导致选中 `"op_2"` 下的目标点时无法更新 `selectedOperationId_`。此外第 31 行早退条件 `(!operationId.empty() && selectedOperationId_ == operationId)` 在 `operationId` 为空时永远不成立。
  2. `ShellWindow::applySelection()` 在 `selectedTargetId()` 非空时于第 959 行直接 `return`，未执行第 973 行的 `rebuildTargetTable()`，导致跨操作选中目标点时 `targetTable_` 仍停留在上一个操作的行数据。
- **修复要求**：
  - 在 `MainWindow::selectTarget` 中使用局部变量 `std::string foundOpId;` 检索所属操作，找到后赋给 `selectedOperationId_` 并跳出循环；修正早退条件支持默认空 `operationId`。
  - 在 `ShellWindow::applySelection()` 处理非空 `selectedTargetId()` 时，若 `targetTable_` 为空或其首行记录的操作 ID（`Qt::UserRole + 1`）不等于 `controller_.selectedOperationId()`，先调用 `rebuildTargetTable()` 再高亮对应行。

### M08-006 [OPEN] 补充 `Test 15` 回归断言（P1）
- **位置**：`tests/test_main.cpp:1588-1831`
- **修复要求**：
  - 在 `Test 15` 中补充断言：
    1. 对未修改姿态的操作执行 `SmoothTargetOrientationsCommand` 时不发生变更（`!cmdMgr.canUndo()` 或 `undoCount` 不增加，中间点保持 `!userModified`）；
    2. `RegenerateOperationTargetsCommand` 执行与 `undo()` 对称恢复 `seam.programmed` 与 `doc.isDirty`；
    3. 在含 2 个操作的文档中，先选中 `op_1`，再调用单参数 `win.selectTarget(targetInOp2)`，验证 `win.selectedOperationId() == "op_2"`。
