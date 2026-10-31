# Implementation Report — Task M14-PROJECT-REBIND-002 (Round 1)

- **Task ID**: `M14-PROJECT-REBIND-002`
- **Round**: `1`
- **Status**: `READY_FOR_REVIEW`
- **Executor**: Executor
- **Date**: 2026-10-31

---

## 1. 任务背景与闭环目标

本次任务彻底闭环 `CRDocs/DEFERRED.md` 中记录的两个工程打开与几何一致性延期项：
1. **M14-012（打开工程前检查未保存修改）**：在文档存在未保存修改（`isDirty == true`）时拦截工程打开与窗口关闭，提供非阻塞测试注入钩子，防止 ctest 被原生弹窗挂起；
2. **M14-013（工程打开重新绑定 CAD 形体与失效旧句柄）**：打开工程时彻底清空内核旧会话缓存，阻断旧 B-Rep 跨工程复用风险；重新根据 `sourceCadFile` 去重加载与重新绑定 `cadHandleId`；文件缺失或导入失败时干净置为 -1 并走持久化网格降级回退。

---

## 2. 核心改动实施细节

### 2.1 闭环 M14-012（打开工程前检查未保存修改与 closeEvent 统一拦截）

1. **`ShellWindow` 引入 `confirmDiscardIfDirty()` 统一检查逻辑**：
   - 当 `controller_.document().isDirty == false` 时，直接返回 `true` 放行；
   - 当 `controller_.document().isDirty == true` 时：
     - 若设置了测试/无头注入钩子 `confirmDiscardCallback_`，优先调用该回调；
     - 否则弹出模态确认对话框（`QMessageBox::question`），用户点击取消（`Cancel`）返回 `false`，确认放弃（`Discard`）返回 `true`。
2. **`ShellWindow::openProjectFile` 拦截**：
   - 增加显式重载 `openProjectFile(const QString& explicitPath)` 与无参 `openProjectFile()`；
   - 若 `confirmDiscardIfDirty()` 返回 `false`，立即安全中止（早期 return），当前文档状态与脏标记保持完全不变；
   - 用户确认放弃后才继续执行打开并清除脏标记。
3. **`ShellWindow::closeEvent` 统一拦截**：
   - 重写 `closeEvent(QCloseEvent* event)`，若 `confirmDiscardIfDirty()` 返回 `false`，调用 `event->ignore()` 安全拦截关闭；确认放弃后放行。
4. **非阻塞测试注入钩子**：
   - 提供 `ShellWindow::setConfirmDiscardCallback(ConfirmDiscardCallback callback)`，允许自动化回归测试在无头环境下精准注入用户响应（`true`/`false`），保证 ctest 永不被原生系统弹窗挂起。

### 2.2 闭环 M14-013（工程打开重新绑定 CAD 形体与失效旧句柄）

1. **`OcctCadKernel::clear()` 清空会话缓存**：
   - 在 `include/olp/cad/OcctCadKernel.h` 与 `src/cad/OcctCadKernel.cpp` 中新增 `clear()` 方法；
   - 彻底清空 `solids_`、`nextHandleId_ = 1`，以及 OCCT 的 `shapes_`、`edgeShapes_`、`faceShapes_` 映射，彻底阻断旧工程残留 B-Rep。
2. **`MainWindow::openProject` 重新装入与句柄重绑定**：
   - 载入新工程文档后，首先调用 `cadKernel_.clear()`；
   - 引入规范路径缓存 `std::unordered_map<std::string, std::vector<core::SceneNode>> loadedCadFiles` 实现同文件去重加载；
   - 遍历 `loadedDoc.sceneNodes`：
     - 若 `sourceCadFile` 为空，明确将 `node.cadHandleId` 置为 `-1`；
     - 尝试按绝对路径或相对于当前工程文件的相对路径解析（使用 `olp::io::utf8ToPath` 和 `std::filesystem::exists`）；
     - 若文件不存在或导入失败，明确将 `node.cadHandleId` 置为 `-1`；
     - 若文件存在且导入成功，从导入的 solids 中匹配同名节点或单实体，更新节点的 `node.cadHandleId`；
   - 视口 `OcctViewport::syncScene` 在 `cadHandleId <= 0` 时干净降级回退至工程文件持久化的 `renderMesh` 与 `collisionMesh`，绝不引用上一工程残留几何句柄。

---

## 3. 修改文件与行数门禁清单

| 文件路径 | 职责与变更说明 | 修改后行数 | 门禁判定（< 1000 行软门禁，1500 行硬上限） |
|---|---|---|---|
| `include/olp/cad/OcctCadKernel.h` | 声明 `clear()` | **109** | **OK** |
| `src/cad/OcctCadKernel.cpp` | 实现 `clear()` 清空 solids/shapes/handles | **884** | **OK** |
| `include/olp/ui/MainWindow.h` | 保持接口兼容 | **166** | **OK** |
| `src/ui/MainWindow.cpp` | `openProject` 增加 CAD 清理、相对路径解析与重绑定 | **854** | **OK** |
| `include/olp/ui/ShellWindow.h` | 声明 `confirmDiscard`、`closeEvent` 与测试钩子 | **136** | **OK** |
| `src/ui/ShellWindow.cpp` | 实现脏文档拦截、重载与弹窗注入 | **993** | **OK**（严格 < 1000 行） |
| `tests/test_project_io.cpp` | 补充 M14-012 与 M14-013 自动化回归断言 | **804** | **OK** |
| `CRDocs/DEFERRED.md` | 将 M14-012 与 M14-013 标记为 RESOLVED | **440** | **OK** |
| `CRDocs/STATUS.md` | 更新状态为 `READY_FOR_REVIEW` | **15** | **OK** |

所有修改文件行数均严格在 1000 行限制之下！

---

## 4. 自动化测试与实跑验证证据

在 Windows MSVC 2022（Release 模式）下本机实跑 `Tools\Compile.bat`：

1. **编译构建**：
   - 0 警告，0 错误，exit code 0。
2. **CTest 自动化回归**：
   - `SiFastSuiteOLP_AllTests`：**100% tests passed, 0 tests failed**（运行耗时 3.84 s）；
   - 测试断言总数从 **834 passed 增加至 862 passed，0 failed**（新增 28 项断言全部 PASS）；
   - 新增锁测断言覆盖：
     - **M14-012 锁测**：
       - `M14-012: Project A loaded into MainWindow` [PASS]
       - `M14-012: Initial document is not dirty` [PASS]
       - `M14-012: Dirty prompt hook triggered on open attempt` [PASS]
       - `M14-012: Cancel keeps current document untouched (nodeA intact)` [PASS]
       - `M14-012: Cancel keeps document dirty flag` [PASS]
       - `M14-012: Discard prompt hook triggered on open attempt` [PASS]
       - `M14-012: Confirm discard overwrites and opens Project B (nodeB)` [PASS]
       - `M14-012: Fresh opened project clears dirty flag` [PASS]
       - `M14-012: Clean document (isDirty == false) opens without prompting` [PASS]
       - `M14-012: Clean document opened successfully` [PASS]
       - `M14-012: closeEvent triggers discard prompt hook when dirty` [PASS]
       - `M14-012: closeEvent ignored when user cancels` [PASS]
       - `M14-012: closeEvent triggers discard prompt hook on confirm` [PASS]
       - `M14-012: closeEvent accepted when user confirms discard` [PASS]
     - **M14-013 锁测**：
       - `M14-013: Step imported for baseline test` [PASS]
       - `M14-013: Baseline kernel has loaded handle with valid edges` [PASS]
       - `M14-013: Open project with missing STEP succeeded` [PASS]
       - `M14-013: Prior kernel handles cleared on project open (baseline handle empty)` [PASS]
       - `M14-013: Scene node restored` [PASS]
       - `M14-013: Missing STEP cleanly resets cadHandleId to -1` [PASS]
       - `M14-013: Render mesh preserved for fallback display` [PASS]
       - `M14-013: Collision mesh preserved for fallback collision detection` [PASS]
       - `M14-013: Open project with valid STEP relative path succeeded` [PASS]
       - `M14-013: Both nodes restored` [PASS]
       - `M14-013: Node 1 re-bound to fresh cadHandleId > 0` [PASS]
       - `M14-013: Node 2 re-bound to fresh cadHandleId > 0` [PASS]
       - `M14-013: De-duplicated loading assigns same handleId for same STEP solid` [PASS]
       - `M14-013: Re-bound cadHandleId has valid topological edges in kernel` [PASS]
3. **SiFastSuiteOLP.exe --verify 验证运行**：
   - 工位导入、焊缝拾取、轨迹规划、OPW 运动学与碰撞检查、FANUC .ls 与离线 HTML 生成全链路畅通，exit code 0。

---

## 5. 架构与硬约束核查

1. **OpwKinematics（RISK-001）与 olp_algo 零改动**：
   - `git diff include/olp/algo/ src/algo/` 结果完全为空；
   - `olp_algo` 保持纯算法层，绝对无 Qt 依赖。
2. **单文件行数限制（Rules.md）**：
   - 所有触碰的文件行数均严格小于 1000 行限制。
