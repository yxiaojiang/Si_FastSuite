# SessionBrief.md — 当前接手摘要

> 最近刷新：2026-10-28。主线仍是对标 FASTSUITE Edition 2 的机器人焊接离线编程。工作区：`C:\Users\mimovr\Desktop\TEmp\Si_FastSuite`。Qt 6.12.0 在 `D:\Soft\Qt\6.12.0\msvc2022_64`，OpenCASCADE 8.0.0 在 `C:\vcpkg\vcpkg\installed\x64-windows`。方向决定是 `PlanDocs/DECISIONS.md` 的 `DIR-001`。

## 1. 当前目标与最新进展

- **`M10-COLLISION-FCL-001`（`DIR-001` 第 3 步）已通过审核并闭环（Round 2 APPROVED）**：
  - 用 FCL 精确网格窄相位间隙算法替换了原球对包围盒；
  - 连杆 1～6（STL 网格，米转毫米，URDF 链带 J2/J3 耦合）加 4 个焊枪球，对工件、夹具、工装台和底座进行精确检测；
  - 屏蔽相邻连杆与法兰-焊枪；焊接角色下激活工件对焊丝尖端（Sphere 0）豁免 10 mm；
  - 报出的毫米数严格来自 FCL 精确检测（接触/穿透为 0.0 mm），不回落包围盒中心深度；宽相位 AABB 仅剔除远距对；
  - 单点检测在 UI 线程；72 组自转扫描通过 `TwistScanTask` 纯数据快照在后台线程池异步执行（支持取消）；
  - Gizmo 拖拽微调通过 `GizmoPreviewSnapshot` 纯数据快照在后台执行，求解逆解后检测全臂连杆 1~6 + 焊枪，具备双检 CAS 并发序列防丢帧逻辑；
  - 严格遵守 1000 行限制，拆分出 `FclCollisionQueries`、`ShellChromeHelpers`、`GizmoDragPreviewHandler`，所有文件均 `< 1000` 行；
  - 610 项自动化测试全量通过，`SiFastSuiteOLP.exe --verify` 退出码 0。

## 2. 已经拍板的顺序（`DIR-001`）

1. [DONE] 里程碑表和根目录计划文首已按 `DIR-001` 刷新。
2. [DONE] FCL 单点实测已完成（Test 21，使用 MSVC 14.44）。
3. [DONE] 按根目录 `Plan.md` 的 M10 换成 FCL 精确间隙，自转扫描后台异步，拖拽防抖全臂预览（`M10-COLLISION-FCL-001` 已 APPROVED）。
4. **[NEXT] 运动学金样改为从 URDF 读原点并比较完整姿态。`RISK-001` 继续开着。**
5. [TODO] 播放用烘焙连杆变换驱动外形（`M11-005`）。
6. [TODO] 工程保存先做 JSON。
7. [TODO] 写明 3D PDF 仍未开始（`RISK-003`），并加 T 型接头自动回归。

## 3. 未关闭风险

1. J2/J3 耦合符号、圈数记在哪三轴、法兰相对示教器的位姿，仍无真机或 ROBOGUIDE 对拍（`RISK-001`）。现有 200 组 URDF 金样只和同一条链比过位置和工具 Z 轴。
2. 回退段对前侧板低于 5 mm 来自球对包围盒，可能是误报（`RISK-002`）。网格结果出来之前不当成实测间隙。
3. 客户实景和 3D PDF 五天熔断未开始（`RISK-003`）。HTML 仍是二维 TCP 折线。焊缝清单和离屏多视角仍在 `M13-006`。
4. `M10-FCL-005`：起弧内缩仍只查焊枪球，不查连杆。

## 4. 下一步任务提示词（新会话）

按 `PlanDocs/DECISIONS.md` 的 `DIR-001` 第 4 步执行：
- 将运动学验证金样从当前简化/硬编码原点改为直接从 `fanuc_m10ia_standalone.urdf` 完整解析关节 origin 与 axis；
- 比较前向运动学输出的完整姿态（完整旋转矩阵/四元数，而不仅是位置与工具 Z 轴）；
- 保持 `RISK-001` 开着；
- 先另开 `CRDocs` 任务再改代码，做完写 `IMPLEMENTATION.md` 并交接；
- 编译需带 `/p:VCToolsVersion=14.44.35207`，测试套件确保全量通过。

## 5. 索引

设计基线：`Docs/OLP_System_Design_Claude.md`。排期：根目录 `Plan.md`。活跃事项：`.claude/Plan.md`。计划决定：`PlanDocs/DECISIONS.md`。代码交接：`CRDocs/`。已做步骤：`.claude/contexts/steps/00-index.md`。
