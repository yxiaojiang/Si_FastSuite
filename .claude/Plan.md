# Plan.md — 当前活跃事项

> 最近刷新：2026-11-04 Asia/Shanghai（UTC+8）。28 周排期仍在根目录 `Plan.md`。设计基线仍是 `Docs/OLP_System_Design_Claude.md`（v1.1）。方向决定见 `PlanDocs/DECISIONS.md` 的 `DIR-001`、`DIR-002` 与 `DIR-003`。`DIR-003-STEP2-STAGING-001` 已 APPROVED 闭环并移入 `Plan-archive.md`。`PackagePlan.md` 为打包发布专项实施计划。

## 里程碑达成度

状态只用「完成」「部分完成」「未做」。部分完成不能当成该项验收已经通过。

### M1 端到端骨架

| 验收 | 状态 | 依据 |
| :--- | :--- | :--- |
| 导入整工位 STEP，换成可动 M-10iA | 完成 | 方案 A 与动态 STEP 工位替换导入命令已落地（`DIR-002-STEP-IMPORT-001` APPROVED） |
| TcpGizmo 示教，撤销和重做 | 完成 | 边拾取与手柄已落地，且旋转平移解耦与撤销性能瓶颈已闭环修复 |
| 视口里播放轨迹 | 完成 | TCP 轨迹、TCP 预览球与连杆 0～6 烘焙位姿驱动均已落地（`M11-005`） |
| 导出 `.ls` 并在 ROBOGUIDE 跑出一致轨迹 | 部分完成 | 默认门禁、草稿水印与多程序 Freshness 记账已落地（FIX-EXPORT / MAINT-CLEANUP CLOSED）；仍无上机/ROBOGUIDE 对拍（`RISK-001`） |
| 工程文件保存和打开 | 完成 | `M14-PROJECT-JSON-001` Round 3 APPROVED；`M14-PROJECT-REBIND-002` Round 1 APPROVED（M14-012 脏标记拦截与 M14-013 CAD 句柄重绑定闭环） |

### M2 可演示闭环

| 验收 | 状态 | 依据 |
| :--- | :--- | :--- |
| 箱体工位选边、切入切出、起弧收弧 | 完成 | 选边拾取建缝（防重入）并自动生成操作端到端闭环已打通（`DIR-002-STEP-IMPORT-001` APPROVED） |
| T 型接头走同一条闭环 | 完成 | `Workpiece_TJoint_With_Clamps.step` 端到端闭环自动化回归测试（Test 23）已通过（`M15-TJOINT-REGRESSION-001`） |
| 播放时能看出手臂姿态 | 完成 | 视口已加载 7 节连杆几何外形并由各帧 URDF 链烘焙变换驱动（`M11-005`） |
| 2D PDF 工艺报告 | 部分完成 | `M13-REPORT-PDF-001` 已通过；焊缝清单、问题清单、离屏多视角在 `M13-006` |
| 单文件 HTML 三维动画 | 完成 | `M13-HTML3D-EXPORT-001` Round 2 已 APPROVED；100% 离线自包含 3D 渲染，含工位零件、7 节连杆+焊枪（枪尖与 TCP 物理重合 0 mm）、TCP 轨迹/电弧光晕、转盘相机与倍速播放 |
| 3D PDF 五天熔断 | 完成 | 执行熔断收口（`RISK-003` CLOSED），三维可交互仿真交付统一收口至基于 Web 标准的单文件离线 HTML 3D 动画包，2D 报告由 QPdfWriter 承载 |
| 交互与视口操作体验 | 完成 | 工业级转盘相机（Turntable Orbit）、键位功能映射、Dock 分割条无死锁弹性布局均已落地 |

## 正在做的事

1. **[READY_FOR_DEV] `DIR-003` 独立安装包发布 — 第 3 步（P0-Installer，Inno Setup 单文件安装包与验证）**：
   - 第 0 步已完成：`DIR-002-STEP-IMPORT-001` APPROVED 闭环；
   - 第 1 步已完成：`DIR-003-STEP1-PATH-001`（P0-Path）APPROVED 闭环（`Utf8Path.cpp` 移入 `olp_core`，UTF-8 路径自适应函数 `resolveWorkspaceOrAppDir()`，解耦 `RobotModel.cpp` 与 `SampleSession.cpp`，测试增至 925 项全绿）；
   - 第 2 步已完成：`DIR-003-STEP2-STAGING-001`（P0-Staging）APPROVED 闭环（编写纯英文 `Tools/package_stage.bat`，组装 31 个 vcpkg DLL + 10 个 VC143 CRT + windeployqt Qt 运行时 + `SampleScenes/`，屏蔽外部 PATH 隔离验证 `--verify` 退出码 0，输出解耦零残留）；
   - **当前第 3 步（P0-Installer）**：编写 `installer/SiFastSuite.iss` 与纯英文 `Tools/Package.bat`，调用 `ISCC.exe` 编译单文件安装包 `SiFastSuiteOLP_Setup_v0.1.0.exe` 并完成安装展开、快捷方式启动与卸载无残留验证。
2. **[OPEN] `RISK-001`** — 无 ROBOGUIDE / 真机金样对拍前，J2/J3 耦合符号、圈数轴与法兰相对示教器位姿仍未核验。导出水印与红条只是草稿门禁，不关闭本风险。
3. **[QUEUED] `DIR-002` 后续阶段** — 第 2 步（P0-B，工件/夹具追加导入命令与角色区分）与第 3 步（P1，FANUC 家族多机型参数扩展与 3 处 M-10iA 硬编码解耦）。

已完成的 FIX-EXPORT-SAFETY / DIR-001 / Bugfix / MAINT-CLEANUP / M14-PROJECT-REBIND-002 / DIR-002-STEP-IMPORT-001 / DIR-003-STEP1-PATH-001 / DIR-003-STEP2-STAGING-001 叙事见 `Plan-archive.md`。`RISK-002` 与 DEFERRED 延期项仍有效。
