# M10-COLLISION-REACH-001 — 机器人运动学可达性、奇异点检测、碰撞检测与自转角扫描（实现与验证交接）

> 执行者：Gemini / Claude
>
> 日期：2026-10-26
>
> 轮次：Round 2
>
> 状态：READY_FOR_REVIEW（见 `STATUS.md`）
>
> 对应任务：M10 机器人运动学可达性、奇异点检测、碰撞检测与自转角扫描（解决 Round 1 审核提出的 M10-001 ~ M10-003 缺陷，严格遵循未打开 M10-004 ~ M10-006 延期项约定）。

---

## Round 2 缺陷修复与实现细节

针对 Reviewer 在 `CRDocs/REVIEW.md` 中指出的 3 项 OPEN P1 缺陷，逐项完成闭环修复与自动化回归测试：

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
  - 若 `mutated` 为 true，调用 `validateOperation(opId)`，并通过新增的 `syncRobotJointsFromSelectedTarget()` 自动将解出的关节角写入机器人节点的 `currentJointsRad`，随后触发文档变更通知；
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

---

## 自动化测试与验证证据（Test 17）

在 `tests/test_main.cpp` 的 Test 17 中增补对 Round 1 审核意见的端到端自动化测试：
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

### 构建与测试结果
```powershell
$env:PATH = "D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;$env:PATH"; .\build-full\Release\olp_tests.exe
```
**测试输出**：
- **357 passed, 0 failed**（全绿，包含 Test 17 扩充的全部断言）。

```powershell
$env:PATH = "D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;$env:PATH"; .\build-full\Release\SiFastSuiteOLP.exe --verify
```
**端到端输出**：
- 5 个阶段完整跑通，生成 764 帧离线轨迹，导出 FANUC .ls 与 HTML 离线预览，退出码为 0。
