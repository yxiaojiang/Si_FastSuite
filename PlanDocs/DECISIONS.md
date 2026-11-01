# 已采纳的计划决定

## DIR-001（2026-10-28）先做碰撞实测，再按顺序补闭环

这次讨论发生在本目录建好之前，收口后补记在这里。

**决定**：停止打磨已经通过的 M13 列宽。不另写 `Docs/` 基线。工程保存往后放。碰撞先做收窄后的单点实测，不在实测这一步替换正式间隙。

**顺序**：

1. 在 `.claude/Plan.md` 写 M1/M2 达成度，并改掉根目录 `Plan.md` 文首过时的下一步。
2. 安装 FCL 并确认 CMake 找到它。用现有连杆 STL（米转毫米）和现有 4 个焊枪球，对一个夹具网格测一次距离和耗时。不新做圆柱或锥体焊枪。
3. 再按根目录 `Plan.md` 的 M10 规则替换正式间隙。报出的数字来自精确检测，包围盒只丢掉明显离得远的一对。列出会改基准的测试。72 组自转扫描按实测决定是否进后台；拖拽预览按设计走工作线程，只保留最新请求。
4. 运动学金样改为从 URDF 读关节原点，并比较完整姿态。
5. 播放时用烘焙连杆变换驱动外形。
6. 工程保存先做 JSON。
7. 用一段话结束 3D PDF 的悬置，并加 T 型接头自动回归。

**未决假设**：J2/J3 耦合符号仍是 +1，没有示教器对拍（`RISK-001`）。回退段低于 5 mm 仍可能是包围盒误报（`RISK-002`）。单点耗时还没有测到。

**计划位置**：`.claude/Plan.md`、`.claude/SessionBrief.md`。

---

## DIR-002（2026-11-01）动态导入外部 STEP 工位与选边闭环，后续解耦 FANUC 机型

**决定**：
不另写 `Docs/` 基线。严禁从纯 STEP 几何中逆向推测机器人运动学。分阶段推进：阶段一（P0）前置拆分 `ShellWindow.cpp`，打通动态 STEP 整工位替换与追加导入，接通视口点边生成焊缝与路径，并闭环 `M14-014` 与 `M14-015`；阶段二（P1）聚焦 FANUC 家族机型扩展，不新建 `RobotModelRegistry` 包装类，直接在 `RobotModel` 结构内解耦 3 处底层 M-10iA 硬编码。

**顺序**：

1. **第 1 步（P0-A，拆分 `ShellWindow.cpp` + 整工位动态替换导入 + 视口选边建缝闭环）**：
   - 先将 `src/ui/ShellWindow.cpp`（993 行）的菜单与工具栏构建拆分为 `src/ui/ShellWindowChrome.cpp`，修改 `CMakeLists.txt`，将主文件降至 600 行以内；
   - 修正整工位替换命令（`ImportSceneAndReplaceRobotCommand`）：非空文档下清空并快照 `sceneNodes`、`seams`、`controllers` 与 `MainWindow` 选中状态，`undo()` 完整恢复四者，命令内不调用 `cadKernel_.clear()` 以免使旧句柄失效；修正 `ShellWindow::refreshFromDocument()` 的 `sceneChanged` 比对判定；
   - 在 `OcctCadKernel::importStep` 全流程接入 `olp::io::utf8ToPath`（闭环 `M14-015`），激活主界面「导入...」动作，并在新导入动作与 `loadSample` 补齐 `confirmDiscardIfDirty()`（闭环 `M14-014`）；
   - 在 `ShellWindow` 中将 `OcctViewport::edgePicked` 信号接通 `CreateSeamFromPickedEdgeCommand`，并在 `rebuildProgramTree` 补齐 `seam.id`，打通「导入 STEP → 点边生成焊缝 → 生成路径 → 仿真/导出」端到端闭环，补齐无头自动化单测。
2. **第 2 步（P0-B，工件/夹具追加导入）**：
   - 在 `WorkcellLayoutService` 中新增可撤销的追加零件/夹具命令，支持区分 `CollisionRole::Workpiece` 与 `CollisionRole::Fixture` 角色并在文件菜单提供追加子入口。
3. **第 3 步（P1，FANUC 家族多机型参数扩展与 3 处硬编码解耦）**：
   - 不新建类，在 `RobotModel` 内部扩展机型定义；解耦 `FclCollisionQueries.cpp:225-249`（连杆前向几何尺寸）、`OcctViewport.cpp:307-338`（按 `modelId` 索引网格缓存）与 `ShellWindow.cpp:449-460`（点动滑条限位范围）三处硬编码，在 UI 属性面板提供机型切换。

**未决假设**：
第三方无关键字 STEP 默认在 `(0, 0, 0)` 放置机器人；属性面板微调实体 Transform 暂不阻塞第 1 步；阶段二新机型连杆 STL 网格需控制在 5 MB 以内。

**计划位置**：`.claude/Plan.md`、`.claude/SessionBrief.md`。
