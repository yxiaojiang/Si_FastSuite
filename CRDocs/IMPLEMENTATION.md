# M09-PROCESS-TUNE-001 — 工艺事件与路径微调（实现与验证交接）

> 执行者：Gemini / Claude
>
> 日期：2026-10-26
>
> 轮次：Round 2
>
> 状态：READY_FOR_REVIEW（见 `STATUS.md`）
>
> 对应任务：M09 焊接工艺参数模型、Arc Start[n]/Arc End[n] 工艺事件关联、CNT/FINE 终止类型配置、绕焊枪 TCP Z 轴自转角优化与局部姿态微调、解决 DEFERRED.md 中的 UI-001，以及 Round 1 审核提出的 M09-001 ~ M09-003 缺陷修复。

---

## Round 1 实现内容（核对已通过）

1. **焊接工艺参数模型与默认生成规则补全**：
   - 在 `include/olp/core/ProjectModel.h` 中完善 `WeldScheduleDefinition`，包含条件号 `scheduleNumber`（1~32）、预置电流 `currentAmps`（A）、预置电压 `voltageVolts`（V）、送丝速度、焊接速度 `weldSpeedCmPerMin` 与 `weldSpeedMmPerSec`、气体流量及描述。
   - 在 `ProjectDocument` 中维护 `std::vector<WeldScheduleDefinition> weldSchedules;`，新建文档时默认初始化 Schedule 1（180A, 22V, 50 cm/min, 15 L/min MAG）。
   - `SeamAndToolpathAlgo::generateOperationTargets` 默认规则：APPROACH=FINE, WELD_START=FINE+ArcStart[n], WELD_VIA=CNT 50（`useWeldSpeed = true`）, WELD_END=FINE+ArcEnd[n]（`useWeldSpeed = true`）, RETRACT=FINE。
2. **绕焊枪 TCP Z 轴自转角优化与局部姿态微调**：
   - 实现 `rotateTargetAroundTcpZ(Target& target, double angleRad)`：枪轴本地 Z 轴自转，位置严格不变，施密特正交化保持 $\det(R) = 1.0$，累加 `userDeltaOffset` 并打上 `userModified = true`。
   - 封装 `ModifyTargetProcessParamsCommand` 与 `RotateTargetAroundTcpZCommand`，严格实现无实质变更不压栈门禁与 Undo/Redo 对称还原。
3. **UI 交互（解决 UI-001 与表格/属性微调联动）**：
   - 解决 `UI-001`：`targetTable_` 多选 $\ge 2$ 点时执行局部区间 SLERP 插值，区间外点姿态严格不变，未选或单选回退全局平滑。
   - 路径点表格 7 列直观展示与双击编辑运动、速度、平滑度与工艺事件。
   - 属性面板集成 TCP Z 自转角快速步进微调面板（$\pm 1^\circ, \pm 5^\circ, \pm 15^\circ$）。

---

## Round 2 缺陷修复与实现细节

针对 Reviewer 在 `CRDocs/REVIEW.md` 中指出的 3 项 OPEN P1 缺陷，逐项完成闭环修复：

### 1. M09-001 速度单元格解析与往返一致性修复
- **修改文件**：`include/olp/ui/ShellWindow.h`、`src/ui/ShellWindow.cpp`、`tests/test_main.cpp`
- **问题**：原代码直接截断字符串，导致形如 `WELD_SPEED (80 mm/s)` 中括号残留无法解析，且无法通过输入纯数值退出 `WELD_SPEED`。
- **实现方案**：
  - 在 `ShellWindow.h` 中定义 `struct ParsedSpeedCell { double speedValue{0.0}; bool useWeldSpeed{false}; bool valid{false}; };`；
  - 实现通用解析函数 `parseSpeedCellText(const QString& rawText, double fallbackSpeed)`：
    - 去除 `WELD_SPEED`、括号、单位并提取浮点数；
    - 若文本包含 `weld`（如 `WELD_SPEED (80 mm/s)` 或 `WELD_SPEED`），设置 `useWeldSpeed = true`，数值存入 `speedValue`；
    - 若输入明确的线性速度（如 `80 mm/s`、`250`）或角度比例（`50%`），设置 `useWeldSpeed = false`，从而允许从 `WELD_SPEED` 退出；
    - 若单位为 `cm/min`（如 `50 cm/min`），按 $\text{mm/s} = \text{cm/min} \times 10 / 60$ 准确换算；
  - 路径点表格第 2 列统一使用 `formatSpeedCellText(target)` 显示与回读；
  - 增加往返断言覆盖 `WELD_SPEED (80 mm/s)`、`80 mm/s`、`50%`、`50 cm/min` 及 `Target` 格式化再解析的全流程。

### 2. M09-002 防止单元格编辑事件中同步销毁当前 WidgetItem 与刷新守卫嵌套
- **修改文件**：`include/olp/ui/ShellWindow.h`、`src/ui/ShellWindow.cpp`
- **问题**：`itemChanged` 槽内触发命令执行时，文档监听器同步调用 `refreshFromDocument` -> `rebuildTargetTable` -> `setRowCount(0)`，在 Qt 事件派发中途销毁了正在编辑的 `QTableWidgetItem`，且 `rebuildTargetTable` 末尾提前将全局 `refreshing_` 置 false 破坏外层守卫。
- **实现方案**：
  - 引入 RAII 深度计数守卫 `RefreshGuard` 与 `refreshDepth_`，替代原扁平布尔变量；提供 `isRefreshing() const noexcept { return refreshDepth_ > 0; }`；
  - `rebuildTargetTable()` 与 `refreshFromDocument()` 以及 `applySelection()` 均使用 `RefreshGuard` 保护，支持多层安全嵌套，离开作用域自动恢复；
  - 在 `itemChanged` 槽内引入 `inItemChanged_` 作用域标记；
  - `documentListenerId_` 监听器检测到 `inItemChanged_` 为 true 时，不进行同步重构，而是通过 `QTimer::singleShot(0, this, [this]{ refreshFromDocument(); });` 将表格重构递延至 Qt 信号派发彻底完成后执行；
  - 当单元格输入非法格式需要回滚时，同样使用 `QTimer::singleShot(0, ...)` 递延刷新，彻底杜绝当前正处于派发栈中的 `QTableWidgetItem` 被同步销毁的隐患。

### 3. M09-003 解耦姿态手改与工艺手改，路径重生成时精准恢复工艺参数
- **修改文件**：`include/olp/core/ProjectModel.h`、`include/olp/app/CommandManager.h`、`src/app/CommandManager.cpp`、`src/algo/SeamAndToolpathAlgo.cpp`、`tests/test_main.cpp`
- **问题**：原代码仅有 `userModified`（姿态偏移），`SavedUserModification` 在重生成时仅保留姿态 `deltaOffset`，工艺改动在重新生成路径时被默认规则覆盖；且若将工艺改动混入 `userModified`，会导致姿态微调点无法随工艺生成规则更新。
- **实现方案**：
  - 在 `Target` 中增加独立字段 `bool processModified{false};`，解耦姿态修改（`userModified`）与工艺修改（`processModified`）；
  - 在 `ModifyTargetProcessParamsCommand` 中：
    - `TargetParamsSnapshot` 保存原 `processModified` 状态；
    - 执行时标记 `target->processModified = true`；撤销时准确恢复 `target->processModified = snap.processModified`；
    - `ReverseOperationPathCommand` 同步交换前后对应点的 `processModified` 标记；
  - 在 `SeamAndToolpathAlgo::generateOperationTargets` 中：
    - `SavedUserModification` 结构体同时保存 `poseModified`、`deltaOffset`、`processModified`、`motion`、`speedValue`、`useWeldSpeed`、`termination`、`cntValue` 与 `events`；
    - 遍历并匹配旧操作点（接近/退避点按角色匹配，焊接点按弧长比例匹配）；
    - 若 `mod->poseModified` 为 true，恢复其姿态偏移 `userDeltaOffset` 并打上 `userModified = true`；
    - 若 `mod->processModified` 为 true，恢复其用户自定义的工艺参数并打上 `processModified = true`；
    - 若仅调整过姿态而未手动修改工艺（`!mod->processModified`），该点的运动类型、速度、平滑度（FINE/CNT）及工艺事件仍严格按最新的生成规则（`inOutOperation.genParams`）重新计算与写入，不锁死工艺参数。

---

## 自动化测试与验证证据

### 1. 单元与集成测试（`build-full/Release/olp_tests.exe`）
- 扩充 `tests/test_main.cpp` 中的 `Test 16`，新增：
  - **M09-001 往返回归验证**：
    - `parseSpeedCellText("WELD_SPEED (80 mm/s)")` -> `valid=true, useWeldSpeed=true, speedValue=80.0`
    - `parseSpeedCellText("80 mm/s")` -> `valid=true, useWeldSpeed=false, speedValue=80.0`（成功退出 WELD_SPEED）
    - `parseSpeedCellText("50%")` -> `valid=true, useWeldSpeed=false, speedValue=50.0`
    - `parseSpeedCellText("50 cm/min")` -> `valid=true, speedValue=8.3333 mm/s`
    - `parseSpeedCellText("250")` -> `valid=true, speedValue=250.0`
    - 各种运动与速度模式下 `formatSpeedCellText` $\to$ `parseSpeedCellText` 往返完全一致。
  - **M09-003 路径重生成参数保留与解耦回归验证**：
    - 修改 WELD_START 的起弧条件号为 Schedule 3；
    - 修改 WELD_VIA_1 的平滑度为 CNT 80、速度为 85.0、运动为 J；
    - 对 WELD_VIA_2 仅执行 TCP Z 自转 30°（纯姿态修改，`userModified = true, !processModified`）；
    - 执行 `RegenerateOperationTargetsCommand`；
    - 验证 WELD_START 重生成后保留 `processModified = true` 且保留 Schedule 3（未被默认覆盖）；
    - 验证 WELD_VIA_1 重生成后保留 `processModified = true` 且保留 CNT 80、速度 85.0 与 J；
    - 验证 WELD_VIA_2 重生成后姿态自转偏角依然保留（`userModified = true`），但工艺平滑度被按当前生成规则重写为 `CNT 50`，运动类型重写为 `L`，且维持 `!processModified`。
- **测试结果**：
  - 测试用例总数扩增至 **298 passed, 0 failed**，退出码 `0`。

### 2. 端到端流程校验（`SiFastSuiteOLP.exe --verify`）
- 运行命令：
  ```powershell
  $env:PATH = "D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;$env:PATH"
  .\build-full\Release\SiFastSuiteOLP.exe --verify
  ```
- **输出**：全部 5 阶段顺利通过，FANUC .ls 与 HTML TCP 动画正常生成，退出码 `0`。
