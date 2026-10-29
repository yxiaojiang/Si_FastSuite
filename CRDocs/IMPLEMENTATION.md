# FIX-EXPORT-GATE-006 — Round 1：OPEN-P2-GATE005-1（exportAllPrograms ensure）

> 执行者：执行方（Executor / Grok Bot）
>
> 日期：2026-10-29（Asia/Singapore UTC+8）
>
> 轮次：Round 1
>
> 状态：**READY_FOR_REVIEW**
>
> 依据：用户批准修复 OPEN-P2-GATE005-1；GATE-005 REVIEW 残留 P2；`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md` **CLOSED**（本项为小额 follow-up，**未重开**该计划）。

本轮仅补齐 `MainWindow::exportAllPrograms` 与 `exportFanucLs` 同级的导出前仿真保证。未使用 Cursor CloudAgent；全部为 box 本地就地编辑（`/workspace/Si_FastSuite`）。

---

## 一、实际修改内容（真实 diff 摘要）

### 产品行为 — `exportAllPrograms` 先 ensure 再 bulk 导出

**`src/ui/MainWindow.cpp`**（`exportAllPrograms`）
- 对每个 `ControllerUnit`，在调用 `FanucLsPostProcessor::exportControllerPrograms` **之前**：
  - 遍历 `cu.programs`；
  - 对每个 program 以 `id`（空则 `name`）调用 `ensureProgramSimulatedForExport`；
  - 有 collision-relevant 几何时，ensure 内对当前场景重跑 `validateAndSimulateProgram`（既有 GATE-005 实现，本轮未改 ensure 本体）；
  - **任一** ensure 失败 → `allSuccess=false`，向 `errors` 推送  
    `"No program found for export (ensure failed): <label>"`，**跳过**该 controller 的 `exportControllerPrograms`（refuse）；
  - 全部 ensure 成功 → 再调用既有 `exportControllerPrograms`（失败仍累计 `allSuccess`）。
- 空 `doc_.controllers` 行为不变：立即 false + `"Document has no controllers"`。

未改：`ensureProgramSimulatedForExport` / `exportFanucLs` / `LsExportDialog` / `FanucLsPostProcessor` / 头文件 API。

### 测试 — 扩展 GATE-005 stale-sim

**`tests/test_postprocessor.cpp`**（Test 19 末尾）
- 有 Fixture AABB + 空 `lastSimulationResult`：
  - 调用前 const `validateProgramForExport("PART_A")` 仍拒（stale）；
  - `exportAllPrograms` 后 PART_A / PART_B 均不再出现 `"no fresh simulation"` 拒信文案（证明 ensure 已跑且覆盖 controller 下多个 program）；
- 空 controllers 的 `MainWindow`：`exportAllPrograms` → `!ok` 且 `errors` 非空。

---

## 二、明确未改（本项冻结 / 非目标）

- `OpwKinematics`：禁止且未改。
- 根 `Plan.md`、`.claude/Plan.md`、DIR-001。
- **`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md`**：保持 **CLOSED**，未重开、未追加计划项。
- **`PlanDocs/STATUS.md`**：保持 **IDLE**。
- **`CRDocs/REVIEW.md`**：审核方所有，执行者**未编辑**。
- HTML 导出器 / Three.js / GLB；`CommandManager` 大拆分；交叉审计 OPEN-P2-1..6 其他项。
- RISK-001 仍 OPEN。

---

## 三、如何验证

### Windows（权威；用户机 — 待跑）

```cmd
Tools\Compile.bat
```

至少确认：
1. 工程编译成功；
2. `ctest` / `SiFastSuiteOLP_AllTests` 全量或至少 postprocessor 通过；
3. 显式关注断言文案含 `FIX-EXPORT-GATE-006`；
4. 有碰撞几何时 `exportAllPrograms` 路径会 per-program re-sim，不再盲信陈旧 Valid cache。

### box（本执行环境）

- **无**完整 cmake / Qt / OCCT / FCL Windows 工具链；**未在 box 编译，未跑 ctest**。
- **不得**将本 IMPLEMENTATION 写成「已编译通过」——编译证据须来自用户机 `Compile.bat`。

### 逻辑验收清单

| 项 | 预期 | 锁定位置 |
| :--- | :--- | :--- |
| OPEN-P2-GATE005-1 | `exportAllPrograms` 先 per-program ensure，再 `exportControllerPrograms`；ensure 失败 refuse + 收集 errors | `MainWindow.cpp` `exportAllPrograms` |
| 多 program | controller 下每个 program id/name 均 ensure | GATE-006 PART_A + PART_B stale 清除 |
| 空文档 | 无 controllers → false + error | GATE-006 empty controllers 断言 |

---

## 四、构建与测试结果

### Windows（权威）
- `Tools\Compile.bat` exit 0；`SiFastSuiteOLP_AllTests` **805 passed, 0 failed**。
- GATE-006 显式 PASS：缺仿真拒、exportAllPrograms ensure 清拒、逐 program ensure、无 controller 拒。
- box 未编译。**未编造**。

## 五、残留风险 / 说明

1. RISK-001 等主线仍 OPEN。
2. `ensureProgramSimulatedForExport` 仅在「找不到 program」时返回 false；正常遍历 `cu.programs` 时几乎总是成功，随后仍由 `exportToFile` / validate 门禁拦碰撞。本项对齐的是「先 re-sim 再写出」，不是新增碰撞判定。
3. `exportSimEnsured_` 仍为文档级标志（GATE-005 设计）；多 program 依次 ensure 会覆盖 `lastSimulationResult_`，但各 program 的 `target.cache` 在各自 ensure 时已写回。
4. 交叉审计 OPEN-P2-1..6 其他项本轮未处理。
5. box 无编译证据；以 Windows Compile.bat 为准。
6. `PlanDocs/FIX-EXPORT-SAFETY` 保持 CLOSED。
