# SessionBrief.md — 当前接手摘要

> 最近刷新：2026-10-25（正式切回对标 FASTSUITE Edition 2 的机器人焊接离线编程 OLP 系统开发主线，完成 `SampleScenes/` 两套场景核验与 3.4 节方案 A 拍板，生成完整开发计划 `Plan.md`，并启动 CMake + Qt6 + OpenCASCADE 基础 C++ 工程骨架）。工作区：`C:\Users\mimovr\Desktop\TEmp\Si_FastSuite`。

---

## 1. 当前目标与最新进展

1. **主线锁定**：严格依据定版执行方案 [`Docs/OLP_System_Design_Claude.md`](file:///c:/Users/mimovr/Desktop/TEmp/Si_FastSuite/Docs/OLP_System_Design_Claude.md)（v1.1）与全景架构文档 [`Docs/FASTSUITE_OLP_System_Architecture.md`](file:///c:/Users/mimovr/Desktop/TEmp/Si_FastSuite/Docs/FASTSUITE_OLP_System_Architecture.md)，推进对标德国 CENIT FASTSUITE Edition 2 的工业机器人焊接离线编程（OLP）系统研发。
2. **两套真实测试场景（`SampleScenes/`）核验与 3.4 节拍板**：
   - **`Scene1_TJoint_And_FANUC_M10iA`**：包含 ROS-Industrial 官方 `FANUC M-10iA / ARC Mate 100iC` 独立 URDF（已明确 OPW 参数：`a1=150, a2=-150, b=0, c1=450, c2=600, c3=640, c4=100` mm，以及 J1~J6 轴向与软限位）、全套视觉/碰撞 STL 网格，以及 3 件典型焊接 STEP 工件（含 4 实体的 T 型角焊带左右夹具总装 `Workpiece_TJoint_With_Clamps.step`、管法兰 `Workpiece_PipeFlange_DN15.step`、U 型支架 `Workpiece_U_Bracket_L70.step`）。
   - **`Scene2_Full_Workcell_STEP`**：包含 12 实体的完整焊接工位 STEP 总装 `Full_Welding_Cell_With_Robot.step`（含底座 `Robot_Pedestal_Base`、5 个静态机器人外形部件 `FANUC_M10iA_Static_*`、工装台 `Welding_Fixture_Table`、3 个箱体角焊工件实体与 2 个压块夹具），外加方管 `Workpiece_SquareTube_80x80x5.step` 与钢吊架 `Workpiece_SteelHanger_SAET200.step`。
   - **3.4 节正式拍板**：采用 **【方案 A：整场景 STEP 导入 + 用机器人库中同型号可动机器人替换静态机器人外形】（P0 默认，工期 1 周）**，并将 `FANUC M-10iA` 直接作为 M05 内置机器人库首款标准型号。
3. **完整开发计划落地**：已将涵盖 M01~M15 全部模块、6.1~6.5 关键算法、阶段 0~阶段 5（28 周，M1/M2/M3 三大里程碑）、风险矩阵与 9 项量化验收指标的详细开发计划写入 `.claude/Plan.md` 与根目录 `Plan.md`。
4. **C++ 基础工程骨架搭建**：正在搭建基于 CMake + C++20 + Qt6 + OpenCASCADE 的五层模块化工程骨架（`olp_core`、`olp_cad`、`olp_device`、`olp_algo`、`olp_io`、`olp_app`、`olp_ui` 及 `olp_tests`）。

---

## 2. 关键技术约束与未关闭风险

1. **五层解耦与命令栈铁律**：算法层（`olp_algo`）严禁依赖 Qt 和视图，100% 纯 C++ 可独立单测；UI 所有数据修改强制走 `CommandManager`（Undo/Redo 命令栈），在阶段 1 第一天即强制落地。
2. **三大行业暗坑防护**：
   - **FANUC J2/J3 轴耦合**：示教器显示角与内部几何模型关节角必须经过耦合换算，并通过 ROBOGUIDE 采样点双向比对；
   - **T 型角焊跨零件两侧面识别**：`ICadKernel` 同时支持同实体拓扑邻接面查询与跨实体空间最近点距离（`< 1.0 mm`）贴合面匹配；
   - **焊丝尖端碰撞豁免**：焊接段内对 TCP 焊丝尖端球域与当前待焊工件做碰撞豁免，防止所有焊接点误报碰撞。
3. **`TcpGizmo` 交互规范**：复用 `AIS_Manipulator` 作为视觉手柄但绝不调用 `Attach()` 挂接对象；拖动中后台线程仅计算最新一帧预览 IK 与单点碰撞，松开鼠标才提交单条 `ModifyTargetPoseCommand`。
4. **3D PDF 5 天熔断与 HTML 保底**：单文件离线 HTML 3D 动画包（Three.js + GLB 烘焙关键帧）为 P0 保底交付物；开源 3D PDF 在阶段 0 限时 5 个工作日验证，逾期未通立即熔断。

---

## 3. 下一步计划

1. 完成 CMake + Qt6 + OpenCASCADE 基础 C++ 工程骨架全部源码编写、MSVC 2022 编译构建与自动化单元测试验证。
2. 运行针对 `SampleScenes/` 的阶段 0 预研测试（验证 `Full_Welding_Cell_With_Robot.step` 整场景导入与静态机器人替换、`Workpiece_TJoint_With_Clamps.step` 跨零件焊缝识别、`FANUC M-10iA` OPW 正逆解闭环及 `.ls` 程序导出）。
3. 推进阶段 0 剩余对拍项（ROBOGUIDE 金样本核对与单文件离线 HTML 动画包导出验证）。

---

## 4. 模块与文档索引

- **开发计划与记忆**：`.claude/Plan.md`（同步镜像至根目录 `Plan.md`）、`.claude/Rules.md`、`.claude/ProjectMemory.md`、`.claude/Plan-archive.md`。
- **核心设计基线**：`Docs/OLP_System_Design_Claude.md`（v1.1 主执行方案）、`Docs/FASTSUITE_OLP_System_Architecture.md`（全景概念方案）、`.claude/contexts/OLP-archive.md`。
- **测试场景资产**：`SampleScenes/Scene1_TJoint_And_FANUC_M10iA/`、`SampleScenes/Scene2_Full_Workcell_STEP/`。
- **历史另册归档**：视觉免示教沿边涂胶方案见 `.claude/contexts/VisionEdgeSystem.md` 与 `Docs/Vision_Edge_System_Baseline.md`。
