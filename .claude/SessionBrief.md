# SessionBrief.md — 当前接手摘要

> 最近刷新：2026-11-01 Asia/Shanghai（UTC+8）。主线仍是对标德国 CENIT FASTSUITE Edition 2 的机器人焊接离线编程（OLP）闭环。工作区路径仍为 `C:\Users\mimovr\Desktop\TEmp\Si_FastSuite`。Qt 6.12.0 MSVC2022_64 在 `D:\Soft\Qt\6.12.0\msvc2022_64`，OpenCASCADE 8.0.0 在 `C:\vcpkg\vcpkg\installed\x64-windows`。设计权威基线为 `Docs/OLP_System_Design_Claude.md` v1.1；方向决定见 `PlanDocs/DECISIONS.md` 的 `DIR-001` 与最新采纳的 `DIR-002`。本轮按 Rules.md「保存记忆」整篇刷新本文件与活跃 Plan。

## 1. 当前目标

贯彻 `PlanDocs/DECISIONS.md` 最新采纳的 `DIR-002` 决定，分阶段推进动态 STEP 导入与 FANUC 机器人验证：
- **当前核心活跃任务（DIR-002 第 1 步 P0-A）**：
  1. 前置将 `src/ui/ShellWindow.cpp`（993 行）的菜单与工具栏拆分为 `src/ui/ShellWindowChrome.cpp`，守住 1000 行强制红线；
  2. 修复整工位替换命令（`ImportSceneAndReplaceRobotCommand`）在非空文档下清空旧工位节点与完整快照/撤销语义，命令内不清空 CAD 内核，修正视口同步判定；
  3. `OcctCadKernel::importStep` 接通 `utf8ToPath`（闭环 `M14-015`），激活主界面「导入...」动作，并补齐 `confirmDiscardIfDirty`（闭环 `M14-014`）；
  4. 接通视口 `edgePicked` 信号至 `CreateSeamFromPickedEdgeCommand`，补齐树节点 `seam.id`，打通动态 STEP 导入后的点边建缝与路径生成闭环；
  5. 补齐自动化单元测试，确保 `Tools\Compile.bat` exit 0、全量测试全绿。

## 2. 最新进展（DIR-002 讨论 CONVERGED 并在 DECISIONS.md 采纳收口）

2026-11-01，针对用户提出的“动态导入外部场景以验证更多机器人”需求，经 PlanDocs 双模型独立评审：
- **方案收敛**：明确死几何 STEP 与活运动学数字孪生的本质区别；阶段一（P0）打通动态 STEP 工位与零件导入并接通视口选边；阶段二（P1）聚焦 FANUC 家族机型参数扩展，不新建多余类，解耦 3 处底层 M-10iA 硬编码；
- **讨论收口**：Gemini Argon 审核给出 `CONVERGED` 结论，正式写入 `PlanDocs/DECISIONS.md`（`DIR-002`），`PlanDocs/STATUS.md` 重置为 `IDLE`。

## 3. 先前已完成指针（各一行）

- `M14-PROJECT-REBIND-002`：工程打开未保存脏标记拦截（M14-012）与 CAD 句柄重绑定隔离（M14-013）已 APPROVED，测试增至 862 项全绿。
- `MAINT-CLEANUP-P2-001`：`CommandManager.cpp` 拆分为 477/528 行；单一规范键彻底闭环 P2-A/B/C，已 APPROVED。
- `M13-HTML3D-EXPORT-001`：单文件离线 HTML 3D 动画包升级，工位零件/连杆/焊枪（枪尖与 TCP 物理重合 0 mm）及倍速播放已 APPROVED。
- `FIX-EXPORT-SAFETY`：默认门禁、写出前强制确保重仿真、Near 不短路 Hard、中文路径与 RISK-001 草稿水印等已全部 APPROVED。
- `DIR-001` 步骤 1–7：FCL 精确间隙、运动学 URDF 完整姿态金样、播放用 URDF 连杆外形、工程 JSON 持久化、T 型接头端到端回归等，均已 APPROVED。

## 4. 未关闭风险与残留

1. **`RISK-001` 仍 OPEN**：J2/J3 耦合符号、圈数记在哪三轴、法兰相对示教器的位姿仍无 ROBOGUIDE 或真机金样对拍。导出水印与对话框红条只做草稿门禁，绝不等于已下发真机。
2. **`RISK-002`**：回退段对前侧板间隙低于 5 mm，可能来自球对包围盒的误报。
3. **延期项有效跟踪**：`M14-014`（示例工位脏拦截）与 `M14-015`（重绑定 UTF-8 窄路径）将在 `DIR-002` 第 1 步顺带闭环；其余 `FIX-EXPORT-P2-D`、`MAINT-002`、`M13-HTML3D-003..006`、`M10-FCL-005`、`M15-001` 等详见 `CRDocs/DEFERRED.md`。

## 5. 下一步与硬约束

下一步 **在新对话中以 CRDocs 规程启动 `DIR-002` 第 1 步（P0-A）代码开发**。硬约束必须遵守：`ShellWindow.cpp` 增加行为前必须先拆分文件；`olp_algo` 禁止依赖 Qt；界面状态变更经 `CommandManager` 命令入栈；禁止对本仓库使用 Cursor CloudAgent。

## 6. 索引

- 计划决定：`PlanDocs/DECISIONS.md`（`DIR-001`、`DIR-002`）；`PlanDocs/STATUS.md`（IDLE）。
- 活跃事项：`.claude/Plan.md`。完成归档：`.claude/Plan-archive.md`。
- 代码审查基准：`CRDocs/STATUS.md`；`CRDocs/DEFERRED.md`。
