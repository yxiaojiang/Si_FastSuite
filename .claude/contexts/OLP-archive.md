# OLP-archive.md — 机器人焊接离线编程（OLP）系统规划、双模型复盘与工程落地上下文

> 状态：**当前活跃主线**（2026-10-25 晚：审计修复在 `02b8005`；OCCT 8.0.0 已安装；Qt6 未安装；OCCT 导入路径尚未编译）。

## 0. 2026-10-25 场景核验拍板与 C++ 工程骨架落地

1. **真实场景核验与 3.4 节拍板结论**：
   - `SampleScenes/Scene1_TJoint_And_FANUC_M10iA/`：提供 `FANUC M-10iA / ARC Mate 100iC` 标准 URDF（含精确 OPW 参数 `a1=150, a2=-150, b=0, c1=450, c2=600, c3=640, c4=100` mm）与视觉/碰撞 STL 网格，以及 3 个待焊工件 STEP（含跨零件 T 型角焊带夹具总装 `Workpiece_TJoint_With_Clamps.step`）。
   - `SampleScenes/Scene2_Full_Workcell_STEP/`：提供 12 个 `MANIFOLD_SOLID_BREP` 的焊接工位 STEP `Full_Welding_Cell_With_Robot.step`（底座 `Robot_Pedestal_Base`、5 个静态机器人外形、工装台、3 个箱体工件、2 个压块）。没有单独的「安装法兰过渡台」。
   - **拍板结论**：3.4 节正式采用 **方案 A（整场景 STEP 导入 + 用机器人库中同型号可动机器人替换静态机器人外形，P0，1 周）**，并将 `FANUC M-10iA` 作为 M05 内置机器人库首款标准型号。
2. **CMake + Qt6 + OpenCASCADE 五层 C++20 工程骨架结构**：
   - `src/core` (`olp_core`): 核心数据模型（`Types.h` 4x4 矩阵/四元数/WPR、变换树 `SceneNode`、`ControllerUnit`、`Program`、`Operation`、`Target`、`SeamDefinition`、持久化几何签名）。
   - `src/cad` (`olp_cad`): `ICadKernel` 与 `OcctCadKernel`。装有 OpenCASCADE 时走 `STEPCAFControl_Reader`；无 OCCT 时只接受本仓库的平面方盒 STEP，遇到曲面或跨行实体会失败，不能当成 B-Rep 内核。
   - `src/device` (`olp_device`): `RobotModel`、内置 `FANUC M-10iA` 与 URDF 限位读取。偏置和符号从 `resources/robots/fanuc_m10ia.json` 加载。
   - `src/algo` (`olp_algo`): `OpwKinematics`、焊缝与路径、碰撞和仿真。法兰位姿与 URDF `tool0` 对过；ROBOGUIDE 对拍未做。
   - `src/io` (`olp_io`): `.ls` 后处理（无效 IK 缓存和圆弧运动会拒绝写出）以及 2D TCP HTML 预览。没有 M14 工程序列化器，也没有 3D PDF。
   - `src/app` (`olp_app`): `ProjectDocument`、`CommandManager`、`WorkcellLayoutService`（方案 A）。没有 `SelectionManager`、`EventBus` 或独立的 `RobotReplacementService`。
   - `src/ui` (`olp_ui`): `TcpGizmo` 与窗口骨架。OCCT 8.0.0 已装在 `C:\vcpkg\vcpkg\installed\x64-windows`，但产品配置还缺 Qt6，所以 `importStepWithOcct` 和 OCCT 视口都还没在本机编译。headless 主程序退出码 2，原因是 `APPROACH` / `WELD_START` 与 `Table_Clamp_Block_1` 干涉。

---

## 1. 第一阶段原始需求与输出文件索引

- **原始目标**：对标德国 CENIT 公司的 **FASTSUITE Edition 2**，开发一套工业机器人焊接离线编程系统闭环（3D 场景导入 $\rightarrow$ 焊枪尖端 TCP 路径编辑与增删改点 $\rightarrow$ 碰撞检测与姿态避障 $\rightarrow$ 轨迹插补播放预览 $\rightarrow$ 导出发那科 `.ls` 离线程序与可播放预览的 3D PDF）。
- **核心产出物**：
  - Gemini 全景架构方案：`Docs/FASTSUITE_OLP_System_Architecture.md`
  - Opus 工程落地与排期方案（v1.1 最终收敛版）：`Docs/OLP_System_Design_Claude.md`
  - 双模型互审记录：`Docs/advice1.txt`（Gemini 对 Opus v1.0 的 3 条建议）、`Docs/SM.txt`（Opus 采纳并微调的说明）
  - 报价单：`Docs/机器人焊接离线编程软件报价单.xlsx`（4 大模块、11 个项目、165 人/天、总计 ¥363,000.00）
  - 测试场景资产：`SampleScenes/Scene1_TJoint_And_FANUC_M10iA/` 与 `SampleScenes/Scene2_Full_Workcell_STEP/`

## 2. Gemini 与 Opus 是如何互相取长补短的？（关键经验沉淀）

这一阶段的双模型协作非常成功，经过两轮交互达到了 100% 架构收敛，具体互补过程如下：

### 第一轮：Gemini 建全景框架 $\rightarrow$ Opus 砍范围、抓暗坑、查源码
1. **Gemini 的贡献（`FASTSUITE_OLP_System_Architecture.md`）**：
   - 将用户的直观描述映射为标准工业术语（TCP 工具坐标系、工作角/行走角/自转角、正逆运动学 FK/IK、奇异点、后置处理器）。
   - 提出五层架构与 9 大核心模块，论证了为什么焊接选边和自动计算两侧面角平分线姿态必须依赖 **CAD B-Rep 内核**而非普通三角网格（Mesh）。
   - 初步调研了英伟达开源的 `NVIDIA-Omniverse/usd-brep`（底层 SMLib 内核，Apache 2.0 协议）。
2. **Opus 的审阅与强化（`OLP_System_Design_Claude.md v1.0`）**：
   - **实锤否决在 MVP 中使用 `usd-brep`**：Opus 查阅了 `usd-brep` 源码与 `CHANGELOG/KNOWN_ISSUES.md`，发现它 2026-10-08 才刚发布 v1.0.0，其 `OCCT_BREP_IMPORT` 模块只能读 OCCT 原生 `.brep` 文件而不能直接读 `.step/.iges`（读 STEP 依然离不开 OCCT），且已知问题中包含“圆柱、球等闭合曲面修复可能出错”（对管板焊接致命）。因此拍板 **MVP 直接用 `OpenCASCADE (OCCT)`，上层用 `ICadKernel` 接口隔离**。
   - **抓出三大行业级工程暗坑**：
     1. **FANUC J2/J3 轴耦合**：发那科机器人示教器显示的 J3 受 J2 影响，与标准 DH/OPW 模型不同，不换算会导致关节角输出对不上。
     2. **焊丝尖端碰撞豁免**：焊接时焊丝尖端（TCP）贴着工件，必须对焊枪喷嘴末端与当前待焊工件做碰撞豁免，否则所有焊接点都会误报碰撞。
     3. **T 型角焊跨零件两侧面识别**：T 型接头中焊缝边属于立板 A，但另一个焊接母材面属于底板 B（不同 CAD Solid），不能只查拓扑邻接面，还要用空间最近点距离去匹配相邻零件贴合面。
   - **架构与排期重构**：
     - 将 `SeamDefinition`（工艺几何）与 `Target`（生成路径）解耦，并设计 `userModified` 标记以在重生成路径时保留人工修改。
     - 指出 `Undo/Redo` 命令栈必须在阶段 1 建立，不能放到最后；并增加 **阶段 0（3 周技术预研）** 提前验证 ROBOGUIDE `.ls` 导入、运动学对拍和 3D PDF 可行性。

### 第二轮：Gemini 复审揪出 3 处隐患 $\rightarrow$ Opus 务实吸收并升级 v1.1
1. **Gemini 对 Opus v1.0 提出的 3 条复审建议（`Docs/advice1.txt`）**：
   - **建议 1（操纵器交互）**：不要直接裸用 OCCT 默认的 `AIS_Manipulator` 挂接路径点，因为拖动时每帧直接改对象会刷爆撤销栈且难以配合实时 IK 与碰撞变色。
   - **建议 2（3D PDF 风险熔断）**：开源 U3D + Acrobat 3D JS 动画链路陈旧难调，且新版 Adobe Reader 默认禁用 3D；建议对开源 3D PDF 设限时熔断，并将“单文件离线 HTML5（Three.js）动画导出”提拔为 P0 保底。
   - **建议 3（场景导入边界）**：用户原话要求“将带机器人的场景导入”，而 v1.0 将外部含机器人场景导入放在了 P1，需明确 P0 怎么处理带机器人的场景文件。
2. **Opus 的吸收与务实微调（`Docs/OLP_System_Design_Claude.md v1.1` & `Docs/SM.txt`）**：
   - **针对操纵器（M03 `TcpGizmo`）**：采纳“拖动只预览、松开才提交命令、逆解与碰撞放工作线程且只处理最新请求”；但**不从零重写三维手柄渲染**，而是继续复用 `AIS_Manipulator` 作为视觉手柄（不调用 `Attach()` 挂接对象，只调 `Transform` 取拖动增量），自己处理参考系切换与步进吸附。1 周即可搞定。
   - **针对 3D PDF 与 HTML 动画包（M13）**：将单文件离线 HTML 动画包（`OCCT RWGltf_CafWriter` 写网格 + `tinygltf` 写烘焙关键帧动画 + 内嵌 Three.js）提为 P0 保底（排期调整为更真实的 1.5~2 周）；给 3D PDF 设定阶段 0 **5 个工作日严格熔断**。
   - **针对“导入带机器人的场景”（新增 3.4 节三方案分支）**：极其聪明地提出了低成本中间方案 **方案 A（整场景 STEP 导入 + 用内置库同型号可动机器人替换静态机器人外形，仅需 1 周，默认入 P0）**，同时列出方案 B（运动学定义向导，+3 周）与方案 C（URDF/USD 导入，+1.5~2 周）。

## 3. 第一阶段可复用到当前新阶段（视觉动态沿边系统）的资产

虽然现在不再做离线编程、CAD STEP 导入、`.ls` 文件导出和 3D PDF，但第一阶段沉淀的以下部分仍可部分参考或复用：
- **机器人正逆运动学（M06）**：六轴机器人的正运动学（FK）、OPW 解析逆解（IK）、奇异点与关节限位检查、末端工具坐标系（TCP）标定与变换矩阵运算完全通用。
- **小圆柱/焊枪姿态数学表达**：利用曲线切向 $\vec{T}$ 与表面法向 $\vec{N}$ 构建局部标架（Frame）的方法，可直接迁移到新系统中计算木板边缘的外法向刀补轨迹。
