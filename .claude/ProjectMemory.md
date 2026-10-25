# ProjectMemory.md — 稳定全局契约与项目脉络

> 只放跨阶段、长期有效的项目背景、双模型协作模式与工程硬约束；具体模块细节见 `.claude/contexts/`。最近整理：2026-10-25。

---

## 一、 项目演进脉络（两个阶段）

1. **当前活跃主线（2026-10-25 恢复推进）：对标 FASTSUITE Edition 2 的 CAD 机器人焊接离线编程（OLP）系统**
   - **目标**：导入带机器人的 3D CAD 场景（STEP） $\rightarrow$ 拾取工件焊缝边自动生成带姿态路径 $\rightarrow$ 增删改点与 6D 姿态微调 $\rightarrow$ 可达性与碰撞检测 $\rightarrow$ 运动插补播放预览 $\rightarrow$ 导出 FANUC `.ls` 离线程序与可播放预览的 3D PDF / 单文件 HTML。
   - **成果文档**：`Docs/FASTSUITE_OLP_System_Architecture.md`（Gemini）、`Docs/OLP_System_Design_Claude.md (v1.1)`（Opus 定版主方案）、`Plan.md`（完整开发计划）、`Docs/机器人焊接离线编程软件报价单.xlsx`、测试资产 `SampleScenes/`。
   - **3.4 节方案拍板（2026-10-25）**：基于 `SampleScenes/` 两套场景核验，正式拍板采用 **方案 A（整场景 STEP 导入 + 用内置库同型号可动机器人 `FANUC M-10iA` 替换静态机器人外形）**。算法回归用 `-DOLP_HEADLESS=ON`。产品配置同时要求 Qt6 与 OpenCASCADE。2026-10-25 晚 OCCT 8.0.0 已装在 `C:\vcpkg\vcpkg\installed\x64-windows`，Qt6 尚未安装。
   - **详细技术沉淀**：见 `.claude/contexts/OLP-archive.md` 与 `.claude/Plan.md`。

2. **第二阶段（已归档备查，2026-10-21 ~ 2026-10-22）：基于 3D 视觉的机器人免示教动态沿边涂抹/作业系统**
   - **触发原因**：用户明确调整项目主方向——不再做离线编程软件，而是做**“没有任何编程和调试、无 CAD 模型输入、仅靠视觉感知周围物体形状并动态沿边操作”**的系统。
   - **标杆场景与参考视频**：
     - 参考视频：`C:\Users\mimovr\Videos\NVIDIA\screa.mp4`（上海新时达 STEP SmartRobot 2.0 免示教智能打磨工作站切片）。
     - 用户具象化目标：机器人前方放一块 $20\text{cm}\times 20\text{cm}$ 的木板（或任意形状木板），机器人末端有一个可转的小圆柱（用于涂抹油漆）；下达指令后，机器人通过视觉识别前方的木板，自动用小圆柱在木板边缘缓慢涂抹油漆。
   - **双模型收敛基线（v1.1 终审冻结，2026-10-21）**：
     - Gemini 输出初版全景与 6 模块架构设计（`Docs/Vision_Guided_Robot_Edge_System_Gemini.md`）；
     - Claude Opus 完成独立深度评审与排雷（`Docs/Vision_Edge_System_Design_Claude.md`），提出 C1（接触速度规划）、C2（各向同性浮动）、C3（被动随动滚轮）、C4（蘸漆站与羽化搭接）、C5（上棱视差抑制）等修正；
     - Opus 复核 Gemini 摘要（`Docs/Reivew.md`，B1~B5/C1~C7）→ Gemini 合并为基线 v1.0 → Opus 终审（`Docs/Vision_Edge_System_Final_Review.md`，F1~F8：真空吸附升为 P0 标配以防拧转、人天加总修正、S0-7 条件触发等）→ **Baseline v1.1** 为唯一依据；
     - 系统定型为 M01~M08 软件模块 + HW 工装；P0 12 周（阶段 0 预研 3 周），125~145 人天，27.5~31.9 万元。

---

## 二、 双模型（Gemini + Claude Opus）协作契约

本项目采用 **Gemini 与 Claude Opus 双模型交叉审阅、取长补短** 的工作模式：
1. **分工互补特征**：
   - **Gemini**：擅长多模态理解（如直接解析 `screa.mp4` 视频帧）、全景概念拆解、系统分层架构设计、数学/几何原理推导以及自动化脚本工具（如生成 STEP 几何与完美复刻 Excel 报价单）。
   - **Claude Opus**：擅长工程范围切割（P0/P1/P2 边界）、识别真实工业落地暗坑、设计阶段 0（Spike 预研排雷）与垂直切面迭代计划、制定严密的量化验收标准。
2. **标准协作流程**：
   - Step 1：Gemini 分析需求与视频，输出全景方案文档（如 `Docs/Vision_Guided_Robot_Edge_System_Gemini.md`）并更新 `.claude/` 记忆。
   - Step 2：Opus 读取 `.claude/SessionBrief.md`、`.claude/Plan.md` 与 `.claude/contexts/VisionEdgeSystem.md`，审阅 Gemini 方案，独立输出侧重“范围怎么切、先验证什么、哪里有物理/工程暗坑、怎么验收”的方案文档 `Docs/Vision_Edge_System_Design_Claude.md`。
   - Step 3：Gemini 复审 Opus 的方案文档，提炼双方共识与修正点，形成最终版研发基线，并据此生成配套报价单或启动阶段 0 代码。

---

## 三、 商务与交付物硬约束

- **商务报价单模板**：位于 `C:\Users\mimovr\Desktop\autoCAD二开报价.xlsx`。
  - 报价方信息固定为：`米茂（上海）数字技术有限公司`（上海市闵行区中春路8633万科虹桥云27幢239室），联系人：`陈磊`，电话：`13482519756    021-52233637`，邮箱：`mimo@mimovr.com`。
  - 计价标准默认采用：`2200` `人/天`，所有项目合计（`=G*E`）、模块小计（`=SUM(...)`）与总计必须使用 Excel 动态公式。
  - 生成 `.xlsx` 后必须通过 ZIP 后处理保留原模板的 `xl/theme/theme1.xml`、`xl/drawings/drawing1.xml`（含左上角公司 Logo 的 `srcRect` 裁剪参数）与 `xl/media/image1.png`。