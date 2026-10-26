# M10-COLLISION-REACH-001 — 机器人运动学可达性、奇异点检测、碰撞检测与自转角扫描（实现与验证交接）

> 执行者：Gemini / Claude
>
> 日期：2026-10-26
>
> 轮次：Round 2
>
> 状态：READY_FOR_REVIEW（见 `STATUS.md`）
>
> 对应任务：M10 机器人运动学可达性、奇异点检测、碰撞检测与自转角扫描（解决 Round 1 审核提出的 M10-001 ~ M10-003 以及 Round 2 审核提出的 M10-007 缺陷，严格遵循未打开 M10-004 ~ M10-006、M10-008 延期项约定）。

---

## Round 2 缺陷修复与实现细节

针对 Reviewer 在 `CRDocs/REVIEW.md` 中指出的 OPEN P1 缺陷，逐项完成闭环修复与自动化回归测试：

### 1. M10-001 [P1] 编辑入口执行前读取 `didMutate()` 与撤销/重做缓存对称性修复
- **修改文件**：
  - `include/olp/app/CommandManager.h`、`src/app/CommandManager.cpp`
  - `include/olp/ui/MainWindow.h`、`src/ui/MainWindow.cpp`
  - `src/ui/ShellWindow.cpp`
  - `tests/test_main.cpp`
- **问题**：
  1. `MainWindow` 在 `executeCommand(std::move(cmd))` 之前读取 `cmd->didMutate()`，初值恒为 false，导致函数恒返回 false，`validateOperation` 永远不跑，UI 状态栏将成功错报为失败；
  2. 选中的点自转后，机器人节点的 `currentJointsRad` 停在旧关节角，属性构型与可操作度不刷新；
  3. 撤销快照只存了 `cache.valid`，撤销后回到旧姿态但关节残存新解，重做后无法保持验证结果。
- **实现方案**：
  - `CommandManager::executeCommand` 返回 `bool`（标识命令是否真正执行变更并入栈/合并），三条入口（`smoothTargetOrientations`、`rotateTargetsTcpZ`、`modifyTargetsProcessParams`）直接接收执行后的真实 `mutated` 结果；
  - 若 `mutated` 为 true，统一由 `CommandManager` 的文档变更监听触发后续验证与刷新，消除重复校验与重入；
  - `MainWindow` 构造时监听 `cmdMgr_` 的文档变更事件，文档变化时自动执行 `syncRobotJointsFromSelectedTarget()`；
  - `RotateTargetAroundTcpZCommand` 与 `ModifyTargetProcessParamsCommand` 的快照由原仅存 `cacheValid` 扩展为完整保存 `core::TargetCache cache`；在首次 `undo()` 时自动将验证后的有效缓存与状态封存至 `afterSnapshots_`，`execute()` 重做时对称还原 `afterSnapshots_`，确保无论是撤销还是重做，目标姿态、`userModified`、缓存（逆解、构型、状态、间隙、问题）以及场景机器人关节均 100% 对称一致；
  - `SmoothTargetOrientationsCommand` 同样增加 `afterTargets_` 并在 undo/redo 时对称备份与还原。

### 2. M10-002 [P1] 自转扫描锁定当前求解构型与焊丝尖端角色判定
- **修改文件**：
  - `include/olp/algo/CollisionAndSim.h`、`src/algo/CollisionAndSim.cpp`
  - `include/olp/ui/MainWindow.h`、`src/ui/MainWindow.cpp`
  - `tests/test_main.cpp`
- **问题**：
  1. `scanSelectedTargetTwistAngles` 之前传入未初始化的 `target->config`（恒为默认 `N U T, 0, 0, 0`），导致扫描始终锁在默认构型，而非目标点已解出的真实构型；
  2. 扫描时碰撞检查硬编码 `isWeldingRole=true`，导致接近点（`APPROACH`）与回退点（`RETRACT`）的焊丝尖端插入工件也被豁免，显示为绿色安全。
- **实现方案**：
  - 将 `CollisionAndSimEngine::scanTwistAngles` 的 `requiredConfig` 参数改为 `const std::optional<core::FanucConfig>&`，并新增 `bool isWeldingRole` 参数；
  - 在 `MainWindow::scanSelectedTargetTwistAngles` 中：
    - 若目标点此前已有解（`cache.valid || cache.status != Unchecked`），以其解出的 `cache.solvedConfig` 作为锁定构型，参考关节使用 `cache.jointsRad`；若从未解过，则传 `nullopt` 且参考关节使用 Home，绝不硬锁默认 `N U T`；
    - 严格根据点角色判定 `isWeldRole = (role == WELD_START || role == WELD_VIA || role == WELD_END)`；
  - 在 `scanTwistAngles` 内部将 `isWeldingRole` 传递给 `checkTorchPoseCollision`，确保只有焊接角色的焊丝尖端在母材与豁免部件内豁免，接近/回退点尖端接触母材严格报警（判定为干涉/红色）。

### 3. M10-003 [P1] 段内扫掠碰撞与直线段异常进入问题列表
- **修改文件**：
  - `src/ui/ShellWindow.cpp`
  - `tests/test_main.cpp`
- **问题**：
  1. `ShellWindow::fillIssues()` 之前仅遍历目标点自身端点的 `target.cache.issueMessage`；
  2. 仿真引擎检出的端点间隙合格但中段扫掠穿透夹具（Sweep Collision）、扫掠安全间隙不足（Sweep Clearance）以及直线段无解/构型跳变等，只记录在 `SimulationResult.issues` 中，未能呈现在问题列表面板，用户无法通过列表查看和点击跳转。
- **实现方案**：
  - 改造 `ShellWindow::fillIssues()`：
    - 优先将 `controller_.lastSimulationResult().issues` 中的段内碰撞、扫掠间隙不足、构型跳变等全部注入列表；
    - 补入各操作目标点的独立缓存异常（`TargetCache.issueMessage`），使用 `seen` 集合（`{targetId, description}`）精确去重；
    - 为每个条目绑定 `operationId` 与 `targetId`，硬碰撞/不可达/超限/跳变设为红色（阻断），间隙不足设为橙色（警告）；
    - 用户单击/双击列表中任意段内问题，触发 `controller_.selectOperation(opId)` 与 `controller_.selectTarget(targetId, opId)`，完成路径点表定位、视口手柄挂载与机器人姿态同步。

### 4. M10-007 [P1] 撤销后仿真结果和下游 cache 仍是编辑后的问题修复
- **修改文件**：
  - `include/olp/ui/MainWindow.h`、`src/ui/MainWindow.cpp`
  - `src/ui/ShellWindow.cpp`
  - `tests/test_main.cpp`
- **问题**：
  1. `validateAndSimulateProgram` 会改写程序内每一个点的 cache，并把段内扫掠、直线段无解和构型跳变放进 `SimulationResult`（留在 `lastSimulationResult_`）。但编辑命令（如自转、平滑、工艺修改）的快照只保存了被直接编辑的目标点；
  2. 当执行 `undo()` 时，命令仅恢复自身被编辑的点，未重新触发整程序仿真，导致下游点的关节、构型、状态与 `lastSimulationResult_` 停留在编辑后的状态，造成撤销后问题列表显示过期的扫掠干涉、下游点构型与起点构型失配等问题；
  3. 撤销“由焊缝生成操作”后，操作已被从文档中移除，但 `lastSimulationResult_` 中遗留旧的 `targetId`，造成问题列表依然显示已删除点的条目。
- **实现方案**：
  - 在 `MainWindow` 中实现 `handleDocumentChanged()` 并注册为 `CommandManager` 的文档变更监听器（`addDocumentChangedListener`）：
    - 检查当前选中的操作和点是否已被删除（例如通过撤销生成操作），若已删除则清空选中 ID 并重置 `lastSimulationResult_ = algo::SimulationResult{}`；
    - 自动确定当前有效操作并调用 `validateOperation(opToValidate)`，在任何编辑、Undo 或 Redo 发生时，自动重新对整个程序执行完整的逆解、构型连续性、奇异点与扫掠碰撞仿真，刷新全程序所有目标点（包括下游点）的缓存及 `lastSimulationResult_`；
    - 自动同步场景中机器人节点的关节角 `syncRobotJointsFromSelectedTarget()`；
    - 设置 `inValidation_` 标志防止重入与递归监听；
  - 在 `ShellWindow::fillIssues()` 中，注入 `lastSimulationResult.issues` 前增加对 `issue.operationId` 和 `issue.targetId` 是否依然存在于文档中的校验，彻底杜绝已删除点进入问题列表；
  - 在 `tests/test_main.cpp` 的 Test 17 中增补对 M10-007 的完整断言：
    - 记录编辑前下游目标点（Target 2、Target 3）的关节角、构型字符串以及 `lastSimulationResult.issues`；
    - 对 Target 1 施加 20° 自转，记录编辑后下游点的关节、构型与 issues；
    - 执行 `undo()`，逐项断言下游 Target 2 关节、Target 2 构型、Target 3 关节与 `lastSimulationResult.issues` 完全恢复至自转前；
    - 执行 `redo()`，逐项断言下游点关节、构型与 `lastSimulationResult.issues` 完全匹配自转后；
    - 循环 `undo()` 撤销生成操作直至操作被删除，断言 `lastSimulationResult.issues` 被清空，且 `ShellWindow` 问题列表中已删除目标点的条目彻底消失。

---

## 自动化测试与验证证据（Test 17）

在 `tests/test_main.cpp` 的 Test 17 中增补对 Round 1 & Round 2 审核意见的端到端自动化测试：
1. **M10-001 验证**：
   - 调用 `mainWin.rotateTargetsTcpZ` 传入 0 增量，断言返回 false 且不入栈；
   - 传入 15° 增量，断言返回 true，`userModified` 置 true，缓存重算且 valid，机器人节点关节角同步更新为新解；
   - 执行 `commandManager().undo()`，断言目标点姿态、`userModified`、缓存（valid、joints、config）及机器人节点关节角 100% 还原至自转前；
   - 执行 `commandManager().redo()`，断言姿态、缓存与机器人关节角再次严格与验证结果一致；
   - 调用 `mainWin.smoothTargetOrientations()`，断言返回 true。
2. **M10-002 验证**：
   - 验证 `scanSelectedTargetTwistAngles` 准确以当前已解出的构型为基准进行扫描，0° 当前姿态可达；
   - 工件贴合焊缝根部（z=500）时，焊接角色目标点（`WELD_START`）焊丝尖端接触工件，`collisionFree = true`（尖端豁免有效）；
   - 将同一几何位置的目标点角色设为 `APPROACH`，再次扫描，断言 `collisionFree = false`（接近点尖端不豁免，严格判干涉）。
3. **M10-003 验证**：
   - 在两端点间隙合格的轨迹中间放置夹具部件 `Fixture_Clamp_Middle`，调用 `validateOperation`；
   - 断言 `SimulationResult.issues` 成功捕获扫掠碰撞 `Sweep collides with Fixture_Clamp_Middle`；
   - 构建 `ShellWindow`，断言 `issueList_` 包含该夹具扫掠碰撞条目，且条目携带 `targetId` 与 `operationId` 用于导航。
4. **M10-007 验证**：
   - 记录 Target 1 自转前下游 Target 2/3 的关节、构型与 `lastSimulationResult.issues`；
   - 自转 20° 后，执行 `undo()`，断言 Target 2/3 的关节、构型与 `issues` 逐项与自转前相等；
   - 执行 `redo()`，断言 Target 2/3 的关节、构型与 `issues` 逐项与自转后相等；
   - 循环 `undo()` 撤销生成操作直至操作被删除，断言 `lastSimulationResult.issues` 清空，`ShellWindow` 中已删除目标点条目为 0。

### 构建与测试结果
```powershell
$env:PATH = "D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;$env:PATH"; .\build-full\Release\olp_tests.exe
```
**测试输出**：
- **368 passed, 0 failed**（全绿，包含 Test 17 扩充的全部断言）。

```powershell
$env:PATH = "D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;$env:PATH"; .\build-full\Release\SiFastSuiteOLP.exe --verify
```
**端到端输出**：
- 5 个阶段完整跑通，生成 764 帧离线轨迹，导出 FANUC .ls 与 HTML 离线预览，退出码为 0。
