# Plan-archive.md — 已完成与历史归档计划

## 2026-10-25 夜：演示焊缝起点躲开夹具

1. **[DONE] 夹具干涉。** 默认 5 mm 起弧时，喷嘴球吃进 `Table_Clamp_Block_1`。没有放宽退出码，也没有改角平分线。`extendStartTrimUntilTorchClears` 把起点收到 32 mm，安全间隙 5 mm 满足。`build-full` 测试 35 项通过，主程序退出码 0。
2. **决定**：暂不安装 ROBOGUIDE，耦合符号保持待核。

## 2026-10-25 夜：Qt 6.12 已安装，曲面 STEP 已由 OCCT 导入

1. **[DONE] Qt 6.12.0 MSVC 2022 64 位**装在 `D:\Soft\Qt\6.12.0\msvc2022_64`。产品配置在 `build-full`，`OLP_HEADLESS` 关闭，并设置 `VCPKG_MANIFEST_MODE=OFF`，避免 vcpkg 源码编译 Qt。
2. **[DONE] 四件曲面 STEP**（管法兰、U 型支架、方管、钢吊架）由 `importStepWithOcct` 导入。`build-full` 的 `olp_tests` 33 项通过，确认有非直线边和非平面。
3. **未完成，转入活跃计划**：夹具碰撞退出码 2、OCCT 装配实体名、ROBOGUIDE 对拍、客户实景与 3D PDF 熔断。

## 2026-10-25 晚：审计修复已推送，OpenCASCADE 已安装

`.claude/Plan.md` 按记忆规则收成 3 条活跃事项。完整排期没有删，仍在根目录 `Plan.md`。

1. **[DONE] 独立审计对应的代码修复**已提交并推送 `02b8005`。headless 测试 33 项通过。运动学按 URDF 链对过；曲面 STEP 在无 OCCT 时拒绝导入；导出和编译脚本失败时返回非零。
2. **[DONE] OpenCASCADE 8.0.0（freetype）**装在 `C:\vcpkg\vcpkg\installed\x64-windows`。`importStepWithOcct` 尚未在本机编译。
3. **未完成，转入活跃计划**：Qt6、四件曲面 STEP 的 OCCT 验收、夹具碰撞、ROBOGUIDE 对拍、客户实景与 3D PDF 熔断。

## 2026-10-25 归档：切换回第一阶段 OLP 离线编程系统开发主线（阶段 0 预研与阶段 1 C++ 骨架启动）

> **归档原因**：用户于 2026-10-25 明确指示切回对标 FASTSUITE Edition 2 的机器人焊接离线编程（OLP）系统，严格按照 `Docs/OLP_System_Design_Claude.md (v1.1)` 制定完整详细开发计划写入 `Plan.md`，基于已就绪的两套测试场景 `SampleScenes/` 完成 3.4 节场景导入方案拍板，并正式启动 CMake + Qt6 + OpenCASCADE 基础 C++ 工程骨架搭建。原第二阶段（视觉免示教沿边涂胶演示计划）归档保留如下：

### 原活跃任务快照（2026-10-22 视觉免示教沿边涂胶演示版）
1. **[待用户确认] 演示输入**：现场机器人品牌型号及轨迹下发能力；近距三维相机；开工前备胶于软辊。
2. **[硬件到位后] 6 周演示**：固定机位标定，找到木板沿边涂胶；平移转动后再涂一次，中间不示教。
3. **[本期契约]**：报价单 `20261022-3`（180 人天 × 2,800 元 = ¥504,000，材料费 5 万元，指南总额 55 万元）。

---

## 2026-10-22 范围收缩：v1.1 全系统报价退出本期

用户要求本期只演示固定机位、一块小木板：一键由三维相机找板并沿边涂胶，平移和转动后再成功一次。据此废止 20261021-2（145 人天 / ¥319,000，基金 50 万元）。新报价 20261022-1 为 40 人天 / ¥88,000，基金指南改为 10 万元档。v1.1 正文保留为扩展清单。

## 2026-10-21 归档：报价单与创新基金指南

1. **[DONE] 报价单** `Docs/机器人视觉免示教沿边涂抹系统报价单.xlsx`：复刻 `autoCAD二开报价.xlsx`（主题、Logo `srcRect`、微软雅黑、2200 元/人天、动态公式）。按基线 v1.1 上限 145 人天计价，Excel 计算总计 ¥319,000。阶段内拆分：阶段 0 = 16+10+4，阶段 1 = 8+8+10+8，阶段 2 = 16+12，阶段 3 = 14+10，阶段 4 = 22，工装 = 7。Q1~Q7 未确认，备注写为待确认基线价。
2. **[DONE] 基金指南** `Docs/附件1 国家工程中心创新基金储备项目指南.docx`：按基线 v1.1 填入；模板原件保留。领域工艺装备，周期 12 个月，经费 50 万元档（31.9 万软件 + 18.1 万试制与耗材），TRL 2→5。

## 2026-10-21 归档：第二阶段视觉沿边涂漆系统——双模型方案收敛

1. **[DONE] Gemini 初版架构**：`Docs/Vision_Guided_Robot_Edge_System_Gemini.md`。
2. **[DONE] Opus 独立评审**：`Docs/Vision_Edge_System_Design_Claude.md`（视频逐帧核查、C1~C12、9 类暗坑、S0-1~7、125~145 人天）。
3. **[DONE] Opus 复核 Gemini 收敛摘要**：`Docs/Reivew.md`（B1~B5、C1~C7）。
4. **[DONE] Gemini 合并基线 v1.0**：存档于 `Docs/Vision_Edge_System_Baseline_v1.0.md`。
5. **[DONE] Opus 终审并冻结基线 v1.1**：`Docs/Vision_Edge_System_Final_Review.md`（F1 防拧转 → 真空吸附标配；F2 人天上限 147 → S0-7 条件触发恢复 125~145；F3~F8 摆放公差、接触力窗口、配方参数化、验收生效条件、T0 起算、【待核】恢复）；计算脚本 `Docs/assets/vision_edge/calc_push_rotation.py`。

---

## 2026-10-21 归档：第一阶段——对标 FASTSUITE 的机器人焊接离线编程（OLP）系统设计与报价

> **归档原因**：用户于 2026-10-21 14:28 调整项目主方向，不再做基于 CAD 的离线编程（OLP），改为开发“基于 3D 视觉的机器人免示教木板边缘动态涂漆系统”。第一阶段全部成果完整保留归档。

### 已完成事项清单

1. **[DONE 2026-10-21] FASTSUITE Edition 2 调研与初版全景架构设计（Gemini）**：
   - 核查 CENIT FASTSUITE Edition 2 工作流，完成概念映射（TCP 工具中心点、工作角/行走角/自转角、FK/IK 正逆运动学、BVH 碰撞检测、FANUC `.ls` 后置处理、3D PDF U3D/PRC 导出）。
   - 调研评估英伟达开源 CAD 内核 `NVIDIA-Omniverse/usd-brep`（底层 SMLib 内核 + Apache 2.0 协议 + OpenUSD `omniSolid` Schema）。
   - 输出文档：`Docs/FASTSUITE_OLP_System_Architecture.md`。

2. **[DONE 2026-10-21] 双模型交叉评审与 v1.1 执行方案收敛（Opus + Gemini）**：
   - Opus 审阅初版方案后输出 `Docs/OLP_System_Design_Claude.md (v1.0)`：
     - 源码级核查 `usd-brep`，确认其 `OCCT_BREP_IMPORT` 仅读写 `.brep` 而非直接读 `.step`，且 v1.0.0 对圆柱等闭合曲面修复存在 Known Issues，拍板 MVP 采用 `OpenCASCADE (OCCT)` 并以 `ICadKernel` 接口隔离。
     - 补充三大行业暗坑：FANUC J2/J3 耦合、焊丝尖端碰撞豁免、T 型角焊跨零件两侧面识别。
     - 重组开发节奏为“阶段 0（3 周预研排雷）+ 阶段 1（第 8 周端到端 `.ls` 闭环 + 强制建立 Undo/Redo 命令栈）”。
   - Gemini 复审 v1.0 并提出 3 条改进建议（写入 `Docs/advice1.txt`），Opus 全部采纳并升级为 `Docs/OLP_System_Design_Claude.md (v1.1)`（说明见 `Docs/SM.txt`）：
     - **拖拽控件 `TcpGizmo`**：复用 `AIS_Manipulator` 手柄但不调用 `Attach()` 挂接对象，只截取拖动增量，后台线程“只算最新一帧”预览 IK 与单点碰撞，松开鼠标才提交一条 `ModifyTargetPoseCommand` 入栈。
     - **3D PDF 熔断与 HTML 保底**：将“单文件离线 HTML（Three.js + Base64 GLB 关键帧动画）”提拔为 P0 保底交付物（排期 1.5~2 周），3D PDF 设为阶段 0 限时 5 个工作日严格熔断验证项。
     - **整场景导入范围切分（新增 3.4 节方案 A）**：默认采用“导入整场景 STEP + 拾取静态机器人底座安装面 + 原地替换为内置库同型号可动机器人”（仅需 1 周工期）。

3. **[DONE 2026-10-21] 测试场景资产构建与 OLP 报价单生成（Gemini）**：
   - 在 `SampleScenes/` 下构建两套标准测试场景：
     - `SampleScenes/Scene1_TJoint_And_FANUC_M10iA/`：ROS-Industrial 官方 FANUC M-10iA（ARC Mate 100iC）URDF 与全套视觉/碰撞 STL 网格 + T 型角焊带夹具多实体 STEP + 管法兰/U型支架真实工业 STEP。
     - `SampleScenes/Scene2_Full_Workcell_STEP/`：包含底座、静态机器人、焊接台、L 型箱体工件及夹具的 12 实体整工位 STEP 总装 `Full_Welding_Cell_With_Robot.step`。
   - 严格复刻 `C:\Users\mimovr\Desktop\autoCAD二开报价.xlsx` 生成 `Docs/机器人焊接离线编程软件报价单.xlsx`（4 大模块、11 个开发项目、共 165 人/天，单价 2200 元/人天，总计 ¥363,000.00）。
