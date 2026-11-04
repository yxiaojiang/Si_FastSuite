# Implementation Report — Task DIR-002-STEP-IMPORT-001 (Round 2)

- **Task ID**: `DIR-002-STEP-IMPORT-001`
- **Round**: `2`
- **Status**: `READY_FOR_REVIEW`
- **Executor**: Executor
- **Date**: 2026-11-04

---

## 1. 任务背景与目标（DIR-002 P0-A）

按照 `PlanDocs/DECISIONS.md` 中的 DIR-002 决定，执行第 1 步（P0-A：整工位动态导入与视口选边建缝闭环）：
1. **前置拆分 `ShellWindow.cpp`（硬红线）**：原文件已达 993 行，拆分菜单/工具栏/Dock/信号连接逻辑至独立编译单元 `src/ui/ShellWindowChrome.cpp`，两文件稳定在黄金区间；
2. **修正整工位替换命令（`ImportSceneAndReplaceRobotCommand`）语义与撤销完整性**：
   - 彻底修复非空文档下导入向旧工位 push_back 导致旧实体与双机器人叠加的缺陷；
   - 执行时清空当前文档 `sceneNodes`、`seams`、`controllers` 与 `MainWindow` 选中状态，命令对四项状态完整快照；
   - `undo()` 完整恢复四项状态，命令内部不调用 `cadKernel_.clear()` 以保证 Undo 所需旧 `cadHandleId` 仍然有效；
   - 修正 `ShellWindow::refreshFromDocument()` 场景变化判定；
3. **激活主界面「导入...」并闭环 M14-014 与 M14-015**：
   - 激活主菜单与工具栏中的「导入...」动作，弹出 `QFileDialog` 筛选 `*.step; *.stp`；
   - 导入前调用 `confirmDiscardIfDirty()` 检查脏文档；同时为 `ShellWindow::loadSample` 补齐该检查（闭环 `M14-014`）；
   - `OcctCadKernel` 内部全流程走 `olp::io::utf8ToPath`，支持 Windows 中文路径导入（闭环 `M14-015`）；
   - 未识别到底座关键字时，默认在世界原点 (0, 0, 0) 放置可动机器人；
4. **接通视口选边建缝交互（打通核心编程闭环）**：
   - 在 `ShellWindow` 中接通 `OcctViewport::edgePicked` 信号，调用 `CreateSeamFromPickedEdgeCommand` 向 `doc_.seams` 插入焊缝并选中；
   - 修复 `ShellWindow::rebuildProgramTree` 写入 `seam.id`，支持按选中焊缝生成操作；
   - 确保外部 STEP 导入后能完成「导入 -> 选边生成焊缝 -> 生成路径 -> 仿真/导出」端到端闭环；
5. **Round 2 缺陷闭环（DIR-002-001 与 DIR-002-002）**：
   - 彻底解决视口恢复高亮发出 `edgePicked` 导致撤销建缝重复入栈 Bug（`DIR-002-001`）；
   - 解决工位替换快照与恢复遗漏焊缝选中的问题，以及导入失败时错误清除选中的缺陷（`DIR-002-002`）。

---

## 2. 核心改动实施细节

### 2.1 前置拆分 ShellWindow.cpp 至 ShellWindowChrome.cpp

- **创建 `src/ui/ShellWindowChrome.cpp`（512 行）**：
  - 承载 `ShellWindow::buildChrome()`、`createMenus()`、`createRibbonToolBar()`、`createDocks()` 以及视口信号连接（包括 `edgePicked`、`orbitCompleted` 等）；
  - 更新 `CMakeLists.txt`，将 `src/ui/ShellWindowChrome.cpp` 注册进 `olp_ui` 目标源文件。
- **精炼 `src/ui/ShellWindow.cpp`（567 行）**：
  - 承载窗口生命周期、业务槽函数（`importStepFile`、`loadSample`、`openProjectFile`、`saveProjectFile`、`exportFanucLs`、`runSimulation` 等）、未保存确认逻辑（`confirmDiscardIfDirty`）与属性面板刷新（`refreshFromDocument`、`rebuildProgramTree` 等）；
  - 两文件行数分别稳定在 567 行与 512 行，严格处于 400~600 黄金区间，彻底解除 1000 行强制硬上限风险。

### 2.2 修正整工位替换命令语义与撤销完整性

- **`ImportSceneAndReplaceRobotCommand` 完整四态快照与恢复**：
  - 在 `include/olp/app/WorkcellLayoutService.h` 中引入 `SelectionSnapshot` 结构体及 `GetSelectionFn` / `SetSelectionFn` 回调类型；
  - `execute()` 首次执行时，对现有 `sceneNodes`、`seams`、`controllers` 以及选中状态（`selectedSceneNodeId`、`selectedOperationId`、`selectedTargetId`）进行深度备份，随后调用 `importFullSceneAndReplaceRobot`；
  - `undo()` 时，完整还原文档的 `sceneNodes`、`seams`、`controllers` 与初始选中状态，且**绝不**调用 `cadKernel.clear()`，保证旧实体对应的 `cadHandleId` 在内核中仍然有效且可交互；
  - `redo()` 再次执行时，将文档场景还原为 `afterNodes_` 并清空 `seams`、`controllers` 及选中状态；
  - 修正初次 `execute()` 后 `resultOut_ = nullptr;`，杜绝调用栈局部变量在 `redo()` 时变为悬空指针被重复写导致 MSVC `0xC0000409`（`STATUS_STACK_BUFFER_OVERRUN`）崩溃问题。
- **场景变化精准判定**：
  - `ShellWindow::refreshFromDocument()` 改为比对场景指纹哈希（基于所有 `node.id` 与 `node.localTransform` 矩阵元素累加的 64 位 FNV 哈希 `lastSceneFingerprint_`），确保工位哪怕实体数量相同也能触发完整的视口几何体刷新。

### 2.3 激活「导入...」并闭环 M14-014 与 M14-015

- **激活「导入...」动作与脏检查（闭环 M14-014）**：
  - 在主菜单「文件(&F)」与 Ribbon「文件操作」工具栏中新增「导入...(&I)」动作；
  - 用户触发时，首先调用 `confirmDiscardIfDirty()` 检查未保存修改；同步为 `ShellWindow::loadSample` 加入 `confirmDiscardIfDirty()` 检查，避免加载示例工位静默覆盖未保存工作区，彻底闭环 `M14-014`。
- **全流程 UTF-8 / 中文路径导入（闭环 M14-015）**：
  - `src/cad/OcctCadKernel.cpp` 中全面接入 `olp::io::utf8ToPath`：
    - `hashStepFile`、`importStepBuiltin`、`importStl`、`importStepWithOcct` 全面使用 `olp::io::utf8ToPath` 打开宽字符流；
    - `MainWindow::openProject`：重绑定缓存键与加载统一使用 `resolvedPath.lexically_normal().u8string()`；
    - 自动化测试 `testChinesePathStepImport` 验证包含中文目录与文件名的 STEP 导入 100% 成功，彻底闭环 `M14-015`。
- **无底座默认原点放置**：
  - 当导入的 STEP 文件中未检索到底座关键词时，默认在世界原点 `(0, 0, 0)` 放置可移动机器人，杜绝空指针或位姿未初始化。

### 2.4 接通视口选边建缝交互（打通核心编程闭环）

- **视口边拾取接入 `MainWindow::createSeamFromPickedEdge`**：
  - 在 `ShellWindowChrome.cpp` 中将 `OcctViewport::edgePicked(int cadHandleId, int edgeIndex)` 信号与 `MainWindow::createSeamFromPickedEdge` 槽函数接通；
  - 构造 `CreateSeamFromPickedEdgeCommand` 执行建缝并推入撤销栈，成功后自动选中新焊缝并在视口高亮；
- **打通操作生成与仿真**：
  - 修复 `ShellWindow::rebuildProgramTree`，为焊缝树节点写入 `seam.id`；
  - 用户选边建缝后，点击「生成操作」即可针对选中的焊缝自动生成逼近、焊接、退刀轨迹与工艺事件，完成「导入 -> 选边建缝 -> 生成路径 -> 仿真/导出」端到端闭环。

### 2.5 Round 2 修复：DIR-002-001 与 DIR-002-002 闭环

1. **DIR-002-001 [RESOLVED] 视口恢复高亮不再发出 `edgePicked` 信号，撤销建缝命令正常工作**：
   - 在 `include/olp/ui/OcctViewport.h:101` 与 `src/ui/OcctViewportPicking.cpp:204, 268-278` 中将 `selectEdge` 签名更新为：
     `bool selectEdge(int cadHandleId, int edgeIndex, bool emitSignal = true);`
   - 内部仅在 `emitSignal == true` 时调用 `edgePickedCb_` 与发射 `Q_EMIT edgePicked(...)`；
   - 在 `src/ui/OcctViewport.cpp:501` 的 `syncScene()` 内部，恢复 `lastPickedEdge_` 高亮时显式传入 `false`（`selectEdge(lastPickedEdge_->cadHandleId, lastPickedEdge_->edgeIndex, false);`），彻底杜绝刷新重入；
   - 在 `src/ui/ShellWindowChrome.cpp:312-315` 槽函数中增加 `if (isRefreshing()) return;` 防御层；
   - 在 `tests/test_ui.cpp:282-290` 增加回归单测：验证 `syncScene()` 调用后不再次触发 `edgePicked` 回调，且 `cmdMgr.undo()` 撤销建缝后，命令完整保存在重做栈中（`cmdMgr.canRedo()` 为 true），`redo()` 可重新恢复焊缝。
2. **DIR-002-002 [RESOLVED] 导入选中快照补充焊缝 ID，撤销及失败导入保持选中状态**：
   - 在 `include/olp/app/WorkcellLayoutService.h:19-24` 中为 `SelectionSnapshot` 结构体新增 `std::string seamId{};` 字段；
   - 在 `src/ui/MainWindow.cpp:158-166` 的选中快照与恢复回调中，读取并恢复 `selectedSeamId_`；
   - 在 `src/app/WorkcellLayoutService.cpp:144-152` 与 `:170-179` 中，无论是导入失败还是 `undo()` 回滚，均通过 `setSelection_(beforeSelection_)` 完整恢复包含焊缝在内的全量 4 项选中状态；
   - 在 `tests/test_ui.cpp:483-518` 增加回归单测：
     a) 初始选中焊缝后执行工位替换导入，`undo()` 成功恢复旧工位节点、焊缝、控制器及 `selectedSeamId` 选中状态；`redo()` 再次清空；
     b) 对不存在的 STEP 执行导入失败测试：验证返回值 false，`selectedSeamId` 完好无损，且 `undoCount()` 保持不变（失败命令未入栈）。

---

## 3. 修改文件与行数门禁清单

| 文件路径 | 职责与变更说明 | 修改后行数 | 门禁判定（< 1000 行软门禁） |
|---|---|---|---|
| `src/ui/ShellWindow.cpp` | 拆分后的业务逻辑与槽函数 | **567** | **OK (400~600 黄金区间)** |
| `src/ui/ShellWindowChrome.cpp` | 拆分后的菜单/Ribbon/视口信号接通（含刷新防抖） | **512** | **OK (400~600 黄金区间)** |
| `include/olp/ui/ShellWindow.h` | 声明新增 action 与槽函数 | **147** | **OK** |
| `src/app/WorkcellLayoutService.cpp` | 完整四态快照撤销恢复与悬空指针修复 | **311** | **OK** |
| `include/olp/app/WorkcellLayoutService.h` | 声明快照类型（含 seamId）与回调函数 | **149** | **OK** |
| `src/ui/MainWindow.cpp` | 增加选边建缝、工位替换（含全量快照）方法 | **935** | **OK (< 1000)** |
| `include/olp/ui/MainWindow.h` | 声明选边建缝与导入工位方法 | **183** | **OK** |
| `src/ui/OcctViewport.cpp` | `syncScene` 恢复高亮时禁用信号发射 | **677** | **OK (< 1000)** |
| `src/ui/OcctViewportPicking.cpp` | `selectEdge` 增加 `emitSignal` 可控参数 | **286** | **OK** |
| `include/olp/ui/OcctViewport.h` | 声明 `selectEdge` 信号控制默认参数 | **147** | **OK** |
| `src/cad/OcctCadKernel.cpp` | 全流程 utf8ToPath 宽字符流解析 | **889** | **OK (< 1000)** |
| `CMakeLists.txt` | 注册 `ShellWindowChrome.cpp` | **225** | **OK** |
| `tests/test_cad.cpp` | 中文路径 STEP 导入单测 | **464** | **OK** |
| `tests/test_ui.cpp` | 整工位替换撤销重做、选边建缝与 Round 2 回归单测 | **656** | **OK** |
| `tests/test_main.cpp` | 接入新增测试 | **71** | **OK** |
| `tests/test_support.h` | 声明新增测试 | **38** | **OK** |

---

## 4. 自动化测试与工程验证证据

1. **`Tools\Compile.bat` 运行结果**：
   - MSVC 2022 编译目标 Release：`exit 0`；
   - `ctest` 自动化回归全绿：`1/1 Test #1: SiFastSuiteOLP_AllTests ... Passed 4.37 sec`；
   - 验证断言计数由 **902** 稳步递增至 **910**（+8 项 Round 2 选边信号重入与快照恢复断言），`0 failed`；
   - `SiFastSuiteOLP.exe --verify`：执行并输出验证数据，`exit 0`。
2. **硬约束核验**：
   - `include/olp/algo/OpwKinematics.h` 与 `src/algo/OpwKinematics.cpp` 未做任何改动（`git diff` 为空，恪守 RISK-001）；
   - `olp_algo` 模块未引入任何 Qt 依赖；
   - 工作区内所有修改的源文件均严格小于 1000 行限制。
