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

---

## DIR-003（2026-11-04）软件单文件安装包打包发布与沙盒装配方案（Inno Setup + 运行沙盒）

**决定**：
采用业界成熟的标准向导式单文件安装包交付规范（摒弃启动解压到 Temp 的伪单文件方案）。选用本机已有的 Inno Setup 6 编译单一可执行文件 `SiFastSuiteOLP_Setup_v0.1.0.exe`。安装后将主程序、全套依赖库、Qt 平台插件及附带的 `SampleScenes` 完整展开至安装目录。
通过“31 个 vcpkg DLL 全量收集 + App-Local VC143 CRT + windeployqt 抽取 + `qt.conf` 隔绝”装配绿色沙盒；通过“`src/io/Utf8Path.cpp` 下沉至 `olp_core` + UTF-8 路径自适应函数 `resolveWorkspaceOrAppDir()` + `SampleSession` 写出重定向至临时目录”彻底解耦源码绝对路径硬编码、避免中文路径乱码、杜绝 UAC 写保护阻断与卸载残留。

**顺序**：

1. **第 0 步（前置收口）**：
   - 先按 `CRDocs/REVIEW.md` 修复 `DIR-002-STEP-IMPORT-001`（闭环 `DIR-002-001` 与 `DIR-002-002`），完成审核并取得 `APPROVED`，确保基线干净。
2. **第 1 步（P0-Path，UTF-8 路径自适应与会话输出目录解耦）**：
   - 将 `src/io/Utf8Path.cpp` 下沉编入 `olp_core`（头文件与命名空间保持 `olp::io` 不变），新增统一返回 UTF-8 `std::string` 的 `resolveWorkspaceOrAppDir()`；
   - 改造 `src/device/RobotModel.cpp` 接入 `resolveWorkspaceOrAppDir()`，并将第 107、142 行 `std::ifstream` 改为通过 `olp::io::utf8ToPath` 打开；
   - 改造 `src/ui/SampleSession.cpp`：读取 `SampleScenes/` 使用 `resolveWorkspaceOrAppDir()`；在沙盒环境下将导出的 `WELD_001.ls` 与 `WELD_001_Preview.html` 写向 `std::filesystem::temp_directory_path() / "SiFastSuiteOLP"`，在源码开发环境下保留 `wsRoot + "/build/..."`；
   - 补齐路径自适应与输出重定向单测，确保 `Tools/Compile.bat` 全量测试全绿。
3. **第 2 步（P0-Staging，31 个 vcpkg DLL + CRT 沙盒完整装配与验证）**：
   - 编写纯英文 `Tools/package_stage.bat`：执行 Release 构建、调用 `windeployqt` 部署 Qt 运行时（剔除 `Qt6PrintSupport`）、拷贝全部 31 个 vcpkg DLL（24 个 OCCT 库 + `ccd.dll` + 6 个底层图形库）、拷贝 VS2022 `Microsoft.VC143.CRT` 运行时 DLL、镜像拷贝 `SampleScenes/` 与 `resources/`；
   - 在屏蔽 Qt/vcpkg `PATH` 的独立终端中执行 `build/dist/SiFastSuiteOLP.exe --verify`，验证工位 STEP 载入与仿真全流程通过，测试含中文路径的沙盒运行。
4. **第 3 步（P0-Installer，Inno Setup 单文件安装包与安装/卸载闭环）**：
   - 编写 `installer/SiFastSuite.iss` 配置；
   - 编写纯英文 `Tools/Package.bat` 串联装配与 `ISCC.exe` 编译，生成 `build/installer/SiFastSuiteOLP_Setup_v0.1.0.exe`；
   - 执行真实安装与卸载测试，确认安装后展开完整、桌面快捷方式秒开、卸载后安装目录无残留文件。

**未决假设**：
OCCT 8.0 在清空环境变量后无隐式资源文件依赖，第 2 步沙盒验证予以最终实测闭环。

**计划位置**：`PackagePlan.md`、`.claude/Plan.md`、`.claude/SessionBrief.md`。

---

## VE-DEMO-001（2026-11-05）Vision_Edge 演示闭环（页首收窄范围）分阶段实施

**决定**：
在 `vision-edge` 分支启动 Vision_Edge **演示级**实现。唯一执行权威为 `Docs/Vision_Edge_System_Baseline.md` 页首 2026-10-22 收窄条款。v1.1 正文与 Assessment v0.1 不作本期范围扩张依据。本地三智能体：A 审计划与阶段放行，B 分阶段实现，C 审实现；不使用 Cursor 云端智能体或外部模型。无真机时以 Mock + 无头回归验收；禁止虚构真机验收。

**顺序（VE-D1~VE-D7）**：

1. **VE-D1**：`ICamera`/`IRobot`/`IIO` + Mock + 合成数据集 + CMake/`olp_tests` 接线（无硬件可回归）。
2. **VE-D2**：标定/手眼位姿数据模型（演示用固定夹具外参可加载；不做现场 9 点实测）。
3. **VE-D3**：简化感知——Mock/数据集 → 板位姿 + 闭合上棱点列（基坐标系）；两次摆放输入可切换。
4. **VE-D4**：轮廓拟合（矩形/凸多边形够用）+ 可选 OCCT 瞬态棱柱桥接进场景。
5. **VE-D5**：沿边滚涂轨迹（外法向偏置 R−δ、闭环切线切入、恒速；演示级可先常数 δ，密集 L）。
6. **VE-D6**：复用 IK/FCL/仿真校验 + `.ls` 生成；滚轮工具几何数据化；Mock `IRobot` 执行。
7. **VE-D7**：一键编排状态机（Idle→Capture→…→Mock Execute→Done）+「摆放 A / 摆放 B」端到端无头回归；最小 CLI 或现有壳入口。真机适配器另开阶段，不在本 Topic 假装完成。

**未决假设**：
现场机器人与相机未确认；`RISK-001` 仍 OPEN，真机无人值守禁止声称通过；Linux box 上 Windows/Qt 全量 GUI 构建可能不可用，以无头测试为阶段门禁。

**计划位置**：`.claude/Plan.md`、`.claude/SessionBrief.md`、`PlanDocs/PROPOSAL.md`。


---

## VE-DEMO-001 软件收口（2026-11-06）

**决定**：VE-D1~D7 全部 APPROVED；页首演示范围之 **软件模拟实现 COMPLETE**（`olp_runtime_tests` 211/211）。一键路径关闭 VE-D6-001。不等于真机、现场视觉、FCL 板件验证、GUI 全绿或 RISK-001 关闭。实机验证另开 Topic。工作区未强制 commit（由用户决定）。
