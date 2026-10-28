# SessionBrief.md — 当前接手摘要

> 最近刷新：2026-10-28。主线仍是对标 FASTSUITE Edition 2 的机器人焊接离线编程。工作区：`C:\Users\mimovr\Desktop\TEmp\Si_FastSuite`。Qt 6.12.0 在 `D:\Soft\Qt\6.12.0\msvc2022_64`，OpenCASCADE 8.0.0 在 `C:\vcpkg\vcpkg\installed\x64-windows`。方向决定是 `PlanDocs/DECISIONS.md` 的 `DIR-001`。

## 1. 当前目标与最新进展

- **`M10-COLLISION-FCL-001`（`DIR-001` 第 3 步）已通过审核并闭环（Round 2 APPROVED）**：
  - 用 FCL 精确网格窄相位间隙算法替换了原球对包围盒；
  - 连杆 1～6（STL 网格，米转毫米，URDF 链带 J2/J3 耦合）加 4 个焊枪球，对工件、夹具、工装台和底座进行精确检测；
  - 屏蔽相邻连杆与法兰-焊枪；焊接角色下激活工件对焊丝尖端（Sphere 0）豁免 10 mm；
  - 单点检测在 UI 线程；72 组自转扫描后台异步（支持取消）；Gizmo 拖拽微调后台防抖全臂逆解预览；
  - 严格遵守 1000 行限制；610 项自动化测试全量通过，`SiFastSuiteOLP.exe --verify` 退出码 0。

- **`M06-KINEMATICS-URDF-001`（`DIR-001` 第 4 步）已通过审核（Round 1 APPROVED）**：
  - Test 3 从 `fanuc_m10ia_standalone.urdf` 读关节原点和轴向，金样比较完整姿态与位置；
  - 零位 (890, 0, 1200) mm，工具 Z 朝 +X。200 组重算姿态差最大约 0.000153°，低于 0.001°；
  - `RISK-001` 仍 OPEN。审核方复跑 611 passed、`--verify` 退出码 0。

- **`M11-PLAYBACK-SHAPE-001`（`DIR-001` 第 5 步）Round 2 已通过审核（APPROVED）**：
  - 播放和静止姿态使用 `forwardRobotLinkWorldFrames`。Home、基座 Z = 500 mm 时连杆 4 在 (150, 0, 1700)，连杆 6 与 `link_6` 对齐；
  - `stopPlayback` 不再 `publish`。`invalidate()` 仍只清理一次；
  - 审核方复跑 657 passed，`--verify` 退出码 0。`M11-009` 不挡通过：停止测试没有计划；
  - `RISK-001` 仍 OPEN。第 6 步和第 7 步未做。

- **`M14-PROJECT-JSON-001`（`DIR-001` 第 6 步）Round 3 已通过（APPROVED）**：
  - `M14-009` 关闭。`MoveFileExW` 失败时返回 false 并删除临时文件，不再把临时文件拷到目标路径上。审核方复跑 725 passed，`--verify` 退出码 0（1047 帧，34.7508 秒）；
  - `M14-008`、`M14-010`、`M14-011` 维持关闭。打开前询问（`M14-012`）和 CAD 形体重建（`M14-013`）仍延期；
  - `RISK-001` 仍 OPEN。第 7 步未做。

## 2. 已经拍板的顺序（`DIR-001`）

1. [DONE] 里程碑表和根目录计划文首已按 `DIR-001` 刷新。
2. [DONE] FCL 单点实测已完成（Test 21，使用 MSVC 14.44）。
3. [DONE] 按根目录 `Plan.md` 的 M10 换成 FCL 精确间隙，自转扫描后台异步，拖拽防抖全臂预览（`M10-COLLISION-FCL-001` 已 APPROVED）。
4. [DONE] 运动学金样改为从 URDF 读原点并比较完整姿态（`M06-KINEMATICS-URDF-001` Round 1 APPROVED）。`RISK-001` 继续开着。
5. [DONE] 播放用 URDF 连杆链驱动外形（`M11-PLAYBACK-SHAPE-001` Round 2 APPROVED）。
6. **[DONE] 工程保存先做 JSON（`M14-PROJECT-JSON-001` Round 3 APPROVED）。** `M14-008`～`M14-011` 已关闭。`M14-012`、`M14-013` 仍延期。
7. [TODO] 写明 3D PDF 仍未开始（`RISK-003`），并加 T 型接头自动回归。

## 3. 未关闭风险

1. J2/J3 耦合符号、圈数记在哪三轴、法兰相对示教器的位姿，仍无真机或 ROBOGUIDE 对拍（`RISK-001`）。现有 200 组 URDF 金样已与同一条链比对完整旋转姿态与位置，但示教器基准仍未对拍。
2. 回退段对前侧板低于 5 mm 来自球对包围盒，可能是误报（`RISK-002`）。
3. 客户实景和 3D PDF 五天熔断未开始（`RISK-003`）。HTML 仍是二维 TCP 折线。焊缝清单和离屏多视角仍在 `M13-006`。
4. `M10-FCL-005`：起弧内缩仍只查焊枪球，不查连杆。
5. `M11-009`：停止测试没有计划，`publish` 会直接返回。时间轴仍不分操作节拍（`M11-006`）。
6. `M14-012`：打开工程不提示未保存修改（延期）。
7. `M14-013`：打开工程不重建 CAD 形体，旧的 `cadHandleId` 可能在同一进程多次切换 STEP 时指到旧实体（延期）。

## 4. 下一步任务提示词

- 第 6 步已通过。没有新的执行指令之前，不开始第 7 步。

## 5. 索引

设计基线：`Docs/OLP_System_Design_Claude.md`。排期：根目录 `Plan.md`。活跃事项：`.claude/Plan.md`。计划决定：`PlanDocs/DECISIONS.md`。代码交接：`CRDocs/`。已做步骤：`.claude/contexts/steps/00-index.md`。
