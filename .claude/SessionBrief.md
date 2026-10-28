# SessionBrief.md — 当前接手摘要

> 最近刷新：2026-10-28。主线仍是对标 FASTSUITE Edition 2 的机器人焊接离线编程。工作区：`C:\Users\mimovr\Desktop\TEmp\Si_FastSuite`。Qt 6.12.0 在 `D:\Soft\Qt\6.12.0\msvc2022_64`，OpenCASCADE 8.0.0 在 `C:\vcpkg\vcpkg\installed\x64-windows\bin`。方向决定是 `PlanDocs/DECISIONS.md` 的 `DIR-001`。

## 1. 当前目标与最新进展

- **`M14-PROJECT-JSON-001`（`DIR-001` 第 6 步）已通过（APPROVED）**：
  - `M14-009` 关闭。原子替换安全防御，725 项测试全绿，`--verify` 退出码 0。`M14-012`、`M14-013` 仍延期。

- **`M15-TJOINT-REGRESSION-001`（`DIR-001` 第 7 步）Round 1 已通过（APPROVED）**：
  - **结束 3D PDF 悬置（收口 `RISK-003`）**：经 DIR-001 拍板定案，正式结束 3D PDF 的五天熔断悬置状态：明确不再投入资源自研高脆弱度且受限于 Adobe Reader 专有沙箱环境的开源 U3D/PRC+JavaScript 动画嵌入链路；MVP 离线三维可交互仿真交付统一收口至基于 Web 标准的「单文件离线 HTML 3D 动画包（Three.js + GLB 关键帧烘焙）」，2D 报告由已投产的 QPdfWriter 承载。若后续特定客户有强需求，直接转入商业 SDK 技术选型评估，不在内部开展开源逆向或重度定制。至此，`RISK-003` 正式收口（CLOSED）。
  - **SampleScenes Scene 1 T 型接头完整端到端自动化回归测试（Test 23）**：
    1. 工位导入与装配校验：从 `SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_TJoint_With_Clamps.step` 导入 4 个实体（`Base_Plate`、`Vertical_Web_Plate`、`Fixture_Clamp_Left`、`Fixture_Clamp_Right`），配置 `FANUC M-10iA` 机器人、150mm 焊枪 UTOOL 1 及 UFRAME 1；
    2. 跨零件边拾取与角焊缝识别：识别立板 400 mm 底边 Edge 3 与底板顶面贴合跨实体侧面，生成 400 mm 连续采样焊缝；
    3. 焊枪双侧平分线标架：立板与底板法向严格正交（90°），双侧角平分线严格匹配 45° 空间角（误差 $< 0.01^\circ$），正交右手坐标系行列式 $\approx 1.0$；
    4. 焊丝尖端豁免机制：焊接角色下激活焊丝尖端（Sphere 0）对母材豁免，焊缝根部无碰撞误报；非焊接角色下精准报出接触；枪体对左/右夹具保持严格碰撞检测（精准捕获 `Fixture_Clamp_Left/Right` 碰撞）；
    5. 避障与轨迹生成：这条前边在 5 mm 切边下已经离开夹具，没有再加大切边；Home 到切入点插入 1 个清空点。12 个目标点状态为 Valid，最小间隙 126.179 mm。烘焙 1507 帧，并输出 `TJOINT_001.ls`。
  - 严格带 `/p:VCToolsVersion=14.44.35207` 编译通过；单文件严格遵守 $< 1000$ 行限制；
  - 测试套件全量通过：**752 passed, 0 failed**；`SiFastSuiteOLP.exe --verify` 退出码 0。

## 2. 已经拍板的顺序（`DIR-001`）

1. [DONE] 里程碑表和根目录计划文首已按 `DIR-001` 刷新。
2. [DONE] FCL 单点实测已完成（Test 21，使用 MSVC 14.44）。
3. [DONE] 按根目录 `Plan.md` 的 M10 换成 FCL 精确间隙，自转扫描后台异步，拖拽防抖全臂预览（`M10-COLLISION-FCL-001` 已 APPROVED）。
4. [DONE] 运动学金样改为从 URDF 读原点并比较完整姿态（`M06-KINEMATICS-URDF-001` Round 1 APPROVED）。`RISK-001` 继续开着。
5. [DONE] 播放用 URDF 连杆链驱动外形（`M11-PLAYBACK-SHAPE-001` Round 2 APPROVED）。
6. [DONE] 工程保存先做 JSON（`M14-PROJECT-JSON-001` Round 3 APPROVED）。
7. **[DONE] 结束 3D PDF 悬置（`RISK-003` 熔断收口），并建立 Scene 1 T 型接头端到端自动回归（`M15-TJOINT-REGRESSION-001` Round 1 APPROVED）。**

## 3. 未关闭风险

1. J2/J3 耦合符号、圈数记在哪三轴、法兰相对示教器的位姿，仍无真机或 ROBOGUIDE 对拍（`RISK-001`）。现有 200 组 URDF 金样已与同一条链比对完整旋转姿态与位置，但示教器基准仍未对拍。
2. 回退段对前侧板低于 5 mm 来自球对包围盒，可能是误报（`RISK-002`）。
3. `M10-FCL-005`：起弧内缩仍只查焊枪球，不查连杆。
4. `M11-009`：停止测试没有计划，`publish` 会直接返回。时间轴仍不分操作节拍（`M11-006`）。
5. `M14-012`：打开工程不提示未保存修改（延期）。
6. `M14-013`：打开工程不重建 CAD 形体，旧的 `cadHandleId` 可能在同一进程多次切换 STEP 时指到旧实体（延期）。
7. `M15-001`：Test 23 的平分线用了绝对值，碰撞、近碰撞、奇异和近限位不会让它失败（延期）。本轮实测这些量是对的。

## 4. 下一步任务提示词

- 第 7 步已通过。`DIR-001` 的七步都已落地。HTML 三维动画仍是「未做」。下一步等用户指定。

## 5. 索引

设计基线：`Docs/OLP_System_Design_Claude.md`。排期：根目录 `Plan.md`。活跃事项：`.claude/Plan.md`。计划决定：`PlanDocs/DECISIONS.md`。代码交接：`CRDocs/`。已做步骤：`.claude/contexts/steps/00-index.md`。
