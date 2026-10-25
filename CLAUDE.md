# CLAUDE.md — 项目协作与记忆加载入口

欢迎接手 `Si_FastSuite` 工程！在开始任何分析、回答或文件操作前，请**务必按以下顺序读取项目记忆文件**：

## 1. 必读记忆文件（启动时立即读取）

1. **`.claude/Rules.md`** — 项目记忆结构、上下文压缩规则、双模型（Gemini + Claude Opus）协作规范与工程硬约束。
2. **`.claude/SessionBrief.md`** — 当前项目接手摘要（最新收敛状态）：当前主线已切回**对标 FASTSUITE Edition 2 的机器人焊接离线编程（OLP）系统**，完成 `SampleScenes/` 两套场景核验并拍板 3.4 节方案 A，正在推进阶段 0 预研与阶段 1 CMake + Qt6 + OpenCASCADE 基础 C++ 工程骨架。
3. **`.claude/Plan.md`（及根目录 `Plan.md`）** — 严格基于 `Docs/OLP_System_Design_Claude.md (v1.1)` 制定的完整开发计划（M01~M15 模块细则、6.1~6.5 关键算法、Phase 0~Phase 5 28 周排期与 9 项量化验收指标）。

## 2. 当前主线任务文档（按需深入读取）

- **`Docs/OLP_System_Design_Claude.md`** — Claude Opus v1.1 最终定版执行主方案（MVP 范围切分、3.4 节三方案分支、五层架构、M01~M15 功能点、TcpGizmo 设计、3D PDF 5 天熔断规则、28 周排期与验收标准）。
- **`Docs/FASTSUITE_OLP_System_Architecture.md`** — Gemini 初版全景概念与架构设计文档。
- **`.claude/contexts/OLP-archive.md`** — OLP 系统双模型两轮收敛复盘与核心技术契约。

## 3. 历史归档（仅在需要溯源视觉沿边涂抹方案时查阅）

- **`.claude/contexts/VisionEdgeSystem.md`** 与 **`Docs/Vision_Edge_System_Baseline.md`** — 第二阶段视觉免示教沿边涂胶方案与报价归档。