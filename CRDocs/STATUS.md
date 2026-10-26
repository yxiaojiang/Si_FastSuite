# 当前协作状态

- **Task ID**：`M09-PROCESS-TUNE-001`
- **Task**：M09 工艺事件与路径微调（焊接工艺参数模型、Arc Start[n]/Arc End[n] 工艺事件关联、CNT/FINE 终止类型配置、绕焊枪 TCP Z 轴自转角优化与局部姿态微调、解决 DEFERRED.md 中的 UI-001）
- **Round**：2
- **Status**：**APPROVED**
- **Scope**：
  1. `include/olp/core/ProjectModel.h`：完善 `WeldScheduleDefinition` 模型，并在 `ProjectDocument` 中维护 `std::vector<WeldScheduleDefinition> weldSchedules;`，默认初始化条件号 1（180A, 22V, 50 cm/min）；完善 `SeamAndToolpathAlgo::generateOperationTargets` 默认规则（APPROACH=FINE, WELD_START=FINE+ArcStart[n], WELD_VIA=CNT50+useWeldSpeed, WELD_END=FINE+ArcEnd[n], RETRACT=FINE）。
  2. `src/algo/SeamAndToolpathAlgo.cpp` & `include/olp/algo/SeamAndToolpathAlgo.h`：实现 `rotateTargetAroundTcpZ(Target& target, double angleRad)`，保证 $\det(R)=1.0$ 右手正交系规范性，并正确累积 `userDeltaOffset` 与打上 `userModified = true`。
  3. `include/olp/app/CommandManager.h` & `src/app/CommandManager.cpp`：实现 `ModifyTargetProcessParamsCommand`（运动类型、速度、终止类型 FINE/CNT 及数值、工艺事件）与 `RotateTargetAroundTcpZCommand`，保证无实质变更零入栈门禁、Undo/Redo 与 `isDirty` 严格双向对称。
  4. `src/ui/ShellWindow.cpp`：实现 `UI-001`（多选目标点时提取选中区间首尾 targetId 执行局部区间 SLERP 插值，单选或未选全局回退）；路径点表格直观展示与编辑运动类型、速度、终止类型与工艺事件；属性面板提供 TCP Z 轴自转角微调快速交互入口。
  5. 修复 Round 1 审核提出的 3 项 P1 缺陷：
     - `M09-001`：`parseSpeedCellText` 完善解析与往返支持，支持 `WELD_SPEED (80 mm/s)`、`80 mm/s`（退出 WELD_SPEED）、`50%`、`50 cm/min`（换算为 mm/s）及纯数值；
     - `M09-002`：`refreshDepth_` 与 `RefreshGuard` 深度计数嵌套防破坏，`inItemChanged_` 配合 `QTimer::singleShot(0, ...)` 递延重构，杜绝单元格就地提交时销毁正在派发信号的 `QTableWidgetItem`；
     - `M09-003`：`Target` 新增独立 `processModified`，在 `generateOperationTargets` 中对 `processModified == true` 的点按弧长/角色恢复工艺参数，而仅调整姿态的点（`userModified == true, !processModified`）按当前规则重写工艺参数，不锁死工艺字段。
  6. `tests/test_main.cpp`：新增 Test 16 自动化用例与回归断言，确保 `olp_tests.exe` 全部通过（298/298 passed），`SiFastSuiteOLP.exe --verify` 退出码为 0。
- **Current owner**：执行方（下一任务）
- **Last updated by**：审核方（Reviewer）
- **Last updated at**：2026-10-26

## 本轮交付（Round 2）

1. **M09-001 速度单元格解析与往返修复**：
   - 提取 `parseSpeedCellText` 与 `formatSpeedCellText`，覆盖 `WELD_SPEED (n mm/s)`、明确的 `n mm/s` / `n`（退出 WELD_SPEED）、`n%` 与 `n cm/min`（自动转换为 mm/s）。
   - 补充多组格式化与解析往返断言，全部通过。
2. **M09-002 表格项防销毁与刷新嵌套守卫**：
   - 使用 RAII 计数守卫 `RefreshGuard` 替换布尔变量，解决嵌套刷新提前重置守卫问题。
   - 在 `itemChanged` 提交命令时使用 `inItemChanged_` 标记，文档监听器使用 `QTimer::singleShot(0, ...)` 递延重构，彻底杜绝当前 `QTableWidgetItem` 在信号处理过程中被销毁。
3. **M09-003 路径重生成工艺参数精准保留与姿态手改解耦**：
   - `Target` 增加独立 `bool processModified{false};` 字段，解耦姿态修改（`userModified`）与工艺修改（`processModified`）。
   - `SavedUserModification` 保存所有手改工艺字段并在 `generateOperationTargets` 中精准还原；
   - 姿态手改点在重生成后姿态偏移保留，但其工艺参数按当前规则重写，不锁死工艺参数。
   - 补齐回归测试：改 CNT/条件号重生成保留，纯自转点重生成后 CNT 依然跟随生成规则。
4. **自动化测试与端到端验证**：
   - `build-full/Release/olp_tests.exe`：**298 passed, 0 failed**。
   - `SiFastSuiteOLP.exe --verify`：退出码 `0`。

详见 `CRDocs/IMPLEMENTATION.md`。

## Round 2 审核结论

`M09-001` ~ `M09-003` 已闭环。审核方复跑 `build-full/Release/olp_tests.exe`：**298 passed, 0 failed**。本任务范围 **APPROVED**。新的 P2 为 `M09-008`、`M09-009`，见 `CRDocs/DEFERRED.md`。

## 下一步

- 推进 M10：机器人运动学与奇异点检测（关节限位、奇异区域警示、构型配置字检查）。
