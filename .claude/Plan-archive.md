# Plan-archive.md — 已完成与历史归档计划

## 2026-10-28：M14 工位工程 JSON 序列化与持久化（DIR-001 第 6 步）

1. **[DONE] 工位工程 JSON 序列化与反序列化持久化。** 任务号 `M14-PROJECT-JSON-001`，Round 3 **APPROVED**。`M14-008`、`M14-009`、`M14-010`、`M14-011` 全部闭环解决：
   - 独立纯 C++ Layer 5 `ProjectSerializer` 引擎（`olp_io`），严格定义 Schema Version 1 与 `"SiFastSuite_Project"` 校验门禁；
   - 严格类型安全：格式标识必须为有效字符串且版本号 $\le 1$；解析先至局部对象，异常安全拦截，杜绝异常逃逸和原工程被空对象覆盖；
   - 两阶段原子落盘：先在内存序列化写入 `.tmp` 临时文件，`flush` 并检查 `fail()` 成功后，通过 Win32 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` 原子替换，彻底移除不可靠的拷贝回退，替换失败立即清理临时文件并返回 false，原文件毫发无损；
   - 宽路径（UTF-8 转 `wstring`）支持：彻底解决 Windows ANSI 936（GBK）环境下的中文/特殊字符目录与文件名保存/打开失败（错误码 3）；
   - 100% 往返精度保证：焊缝采样帧全部法向、角平分线、切向量与局部系还原（$< 10^{-9}$），示教微调 `userModified = true`、`userDeltaOffset`、`ArcStart` / `WeaveStart` / `ArcEnd` 工艺事件全量保真还原；`TriMesh` 真实网格顶点、面片与包围盒还原；
   - `MainWindow::saveProject` 成功后清除 `isDirty`；菜单与工具栏打开/保存全闭环联动；
   - 新增独立测试 Test 22（共 68 项针对性断言），全量测试扩充至 725 项全部通过，`--verify` 退出码 0。各文件行数均受控在 1000 行以内。

## 2026-10-28：播放时用 URDF 连杆链驱动外形（DIR-001 第 5 步，M11）

1. **[DONE] 播放时用 URDF 连杆链驱动外形。** 任务号 `M11-PLAYBACK-SHAPE-001`，Round 2 **APPROVED**。`M11-007`、`M11-008` 闭环解决：
   - 视口播放与场景静止姿态均统一使用 `forwardRobotLinkWorldFrames` 驱动 7 节 STL 网格，彻底消除原 OPW 抽象连杆导致的连杆 4 偏离 640 mm 与连杆 6 姿态差 180° 的问题；
   - `stopPlayback` 停止后彻底清除 TCP 球和轨迹预览，姿态复位回场景静止关节，不再重新发布起点帧；
   - 新增连杆 0~6 七节位置（1 mm 门槛）与连杆 6 姿态（1° 门槛）断言，657 项测试全过，`--verify` 退出码 0。

## 2026-10-28：运动学金样改为从 URDF 读关节原点并比较完整姿态（DIR-001 第 4 步，M06）

1. **[DONE] 运动学 URDF 完整姿态金样比对。** 任务号 `M06-KINEMATICS-URDF-001`，Round 1 **APPROVED**。
   - Test 3 运动学金样直接解析 `fanuc_m10ia_standalone.urdf` 的关节原点（xyz/rpy）与轴向，比对完整旋转矩阵 9 分量、3 个基向量与测地角；
   - 200 组姿态对比误差 $< 0.000153^\circ$，位置误差 $< 10^{-12}\text{ mm}$。`RISK-001` 仍保持 OPEN。全量测试 611 项通过，`--verify` 退出码 0。

## 2026-10-28：FCL 精确网格间隙替换与自转/拖拽防抖（DIR-001 第 2/3 步，M10）

1. **[DONE] FCL 窄相位精确网格间隙替换。** 任务号 `M10-COLLISION-FCL-001`，Round 2 **APPROVED**。`M10-FCL-001`～`M10-FCL-004` 闭环解决：
   - 经典模式安装 `fcl 0.7.0`，连杆 1~6 STL 加 4 个焊枪球对工件/夹具/底座进行精确网格距离检测，干涉严格判定 `<= 0.0`；
   - 72 组自转扫描后台异步（支持取消），拖拽微调防抖双检防丢帧全臂逆解预览；
   - 单文件行数限制遵守，拆分独立编译单元；全量测试 610 项通过，`--verify` 退出码 0。

## 2026-10-27：M13 2D PDF 工位工艺指导报告

1. **[DONE] M13 2D PDF 工位工艺指导报告。** 任务号 `M13-REPORT-PDF-001`，Round 8 **APPROVED**。`M13-001`、`M13-002`、`M13-003`、`M13-004`、`M13-007` 关闭。报告在 300 DPI 上放下工位参数、坐标系、节拍、工艺条件和点位表。满量程姿态 726 对 730，坐标边界 825 对 850。`M13-005`、`M13-006`、`M13-008` 仍延期。全量测试 606 项通过。

## 2026-10-27：FANUC .ls 后处理器完备化、合规门禁与 UI 导出（M12）

1. **[DONE] FANUC .ls 后处理器与导出联动（M12）。** 任务号 `M12-POST-FANUC-001`，Round 2 **APPROVED** 关闭。`M12-001`、`M12-002`、`M12-003`、`M12-005` 闭环解决：
   - 完备 FANUC ASCII `.ls` 结构，精确动态生成 `/ATTR` 的 `LINE_COUNT` 与内存估算；
   - 前置合规门禁：程序名严格 1..16 大写字母规范；点名超 16 字符与程序注释超 32 字符直接报错阻断导出，杜绝静默截断；
   - 速度与 CNT 整数四舍五入直出，J 轴超出 1..100%、L 轴不足 1 mm/s、CNT 超出 0..100 报错阻断，杜绝静默改写或夹紧；
   - 关节运动 J 携带 `useWeldSpeed` 非法组合报错阻断，生成文本绝对杜绝 `J P[...] WELD_SPEED`；
   - 工艺指令完整映射（`Arc Start/End`、`Weave Sine/End`、`WAIT`、`DO`、`! comment`）；
   - Home 双模导出（默认注释行 + 可选 GP1 关节点）；
   - 多操作连续点号与多程序控制器批量导出（`exportControllerPrograms`）；
   - 控制器与 UI 联动：`MainWindow` 暴露高层导出接口，新增独立 `LsExportDialog` 暗色等宽预览对话框，全面激活工具栏与文件菜单导出动作；
   - 新增独立测试单元 Test 19（40 项断言），全量测试扩充至 482 项全部通过，`--verify` 退出码 0，文件行数均受控在 900 行以下。

## 2026-10-27：轨迹插补、节拍估算、帧烘焙与时间轴回放（M11）

1. **[DONE] 轨迹插补与时间轴回放（M11）。** 任务号 `M11-TRAJECTORY-001`，Round 3 **APPROVED** 关闭。`M11-001` 闭环解决：
   - 关节运动同参数梯形规划，直线运动位置线性 + 姿态 SLERP 插值，短程增量折角消除 $\pm 180^\circ$ 圈数跳变；
   - 帧烘焙约 30 FPS 输出，精确区分起弧到熄弧的纯焊接时间与空走时间；
   - 新增 `TimelinePanel` 时间轴面板与 `OcctViewportPlayback` 轻量 TCP 预览，支持播放/暂停/停止/步进/拖拽定位/选中点起播，不污染文档与 Undo 栈；
   - 遵循 Rules.md 将大文件拆分在 1000 行以下；新增 Test 18 测试集（58 项断言），测试集增至 425 项全通，`--verify` 退出码 0。

## 2026-10-26：切线连续边链扩展（Edge Chain）与复合曲率焊缝生成（M07）

1. **[DONE] 切线连续边链扩展与复合焊缝生成。** 实现基于几何与拓扑双重校验的切线连续边链扩展算法 `findTangentContinuousEdgeChain`。1. 基于 `BRepAdaptor_Curve` 的端点 3D 几何邻接判定，彻底免疫 STEP 导入模型中由于 `TopAbs_REVERSED` 引起的顶点反序与端点错位；2. 贪心偏角匹配选择偏转角极小值（$\le \theta_{\text{max}}$），确保 $C^1$ 连续过渡；3. 支持环形曲线（如 `Workpiece_PipeFlange_DN15` 的 360° 圆环焊缝）与多段封闭多边形轮廓闭合环识别（`isClosed = true`）；4. 实现 `buildSeamFromEdgeChain` 复合曲线累计弧长均匀采样与两侧母材局部面法向角平分线矩阵计算；5. 升级 `OcctCadKernel::evaluateEdgeAtArcLength` 支持 OCCT `GCPnts_AbscissaPoint` 真实曲面弧长积分，消除曲率边离散坍塌；6. 扩展 `CreateSeamFromPickedEdgeCommand` 支持自动边链扩展与对称撤销重做。通过独立审计员 Argon 深度代码审查并获 PASS 批准，新增 Test 14 自动化测试，全部 126 项测试通过，`--verify` 退出码 0。
2. **随后完成**：转入 M08 焊缝到操作路径生成、切入切出及姿态 SLERP 插值。

## 2026-10-26：TcpGizmo（AIS_Manipulator）挂载与三维交互微调姿态

1. **[DONE] TcpGizmo 挂载与视口交互。** 在 `OcctViewport` 中实现 `AIS_Manipulator` 挂载与三维手柄交互调姿，严格遵守 M03 五项设计原则：1. 通过专用黄色 4mm 球体视觉代理对象（`targetGizmoProxyAis`）挂载，绝不调用 `Attach()` 绑定底层文档数据；2. 支持 Tool、World、UFrame、Seam 四大参考坐标系无缝切换与步长吸附，计算真实局部坐标变换矩阵；针对焊缝模式消除轨道位移跳跃，实现原地姿态微调并按距离匹配最近采样帧；3. 拖拽过程中仅发射预览信号，零文档污染，Undo 栈保持干净；4. 鼠标松开提交单一 `ModifyTargetPoseCommand` 命令，零位移不入栈，Esc 键干净取消恢复初态；5. 多选路径点批量平移/旋转并打包压入单个 `MacroCommand`；6. 视口析构时前置 `clearTargetGizmo()` 确保 OCCT Handle 引用生命周期安全。新增自动化测试 Test 13（23 项断言），全部 103 项测试通过，`--verify` 退出码 0。
2. **随后完成**：见下一活跃推进项。

## 2026-10-26：真实零件 B-Rep 网格渲染与焊缝边拾取（Edge Picking）

1. **[DONE] B-Rep 渲染与边拾取。** `OcctCadKernel` 在导入 STEP 时持久化保存各 Solid 的 `TopoDS_Shape` 与边拓扑序列 `edgeShapes_`。通过 `BRepMesh_IncrementalMesh` 生成精细三角网格与粗碰撞网格，结合 `BRepTools::Clean` 彻底消除粗细离散污染。视口直接读取真实 CAD 实体创建 `AIS_Shape`，工件 SteelBlue、夹具 Orange、机器人 GoldenRod，支持 `ShadedWithEdges`（黑边界线）。焊缝通过 `BRepBuilderAPI_MakePolygon` 生成青色三维亮线。激活 `TopAbs_EDGE` 选择模式，像素容差 5px，悬停高亮，4px 拖动解耦。通过 `SelectDetected()` 识别边，经剥离局部变换的拓扑与几何双重校验映射回 `edgeIndex`，以 4.0 像素品红色粗线高亮显示，发射 `edgePicked` 信号。实现 `CreateSeamFromPickedEdgeCommand` 撤销/重做命令（严格保证 clean/dirty 对称）。工具栏「拾取」按钮正式启用。新增自动化测试 Test 12，`olp_tests` 78 项全过，`--verify` 退出码 0。

## 2026-10-26：三维视口背景防黑屏物理隔离

1. **[DONE] 视口背景防黑屏物理隔离。** Qt 6 Windows 平台忽略 `WA_PaintOnScreen`，在 `paintEvent`（OpenGL `SwapBuffers`）后 `QBackingStore::flush()` 会对 `OcctViewport` 执行全黑 `GDI BitBlt` 覆盖。现由 `OcctViewport` 内部建立专用 Win32 透传子窗口 `SiFastSuiteOcctRenderHost`（`WM_NCHITTEST -> HTTRANSPARENT`）承载 `WNT_Window`，配合父级 `WS_CLIPCHILDREN` 硬件裁剪与全局 `AA_DontCreateNativeWidgetSiblings`，彻底阻断 GDI 覆盖黑屏与停靠面板原生句柄传染。`olp_tests` 56 项全过，`--verify` 退出码 0。
2. **未完成，转入活跃计划**：视口边拾取，以及把焊枪拖拽画到视口上。

## 2026-10-26：三维视口相机

1. **[DONE] 视口与分区契约。** `OcctViewport` 由窗口壳创建。背景保持深灰。点到其它面板时拦住窗口擦除并整幅重画，避免黑底和底边条纹。左键旋转，中键或右键平移，滚轮向前拉近、向后拉远。「全景」按可见包围盒取景。工具栏按文件、编辑、工作台、编程、视图分组，图标在上、文字在下；还没接入的命令是灰的。场景画成包围盒。两个工作台都保留场景、属性和点动；编程台额外显示程序树和「路径点 | 问题」。程序树不列点，路径点表只含当前操作。问题读路径点缓存。点动读场景机器人且不入栈。`olp_tests` 五十六项通过，`--verify` 退出码 0。
2. **未完成，转入活跃计划**：边拾取，以及把焊枪拖拽画到视口上。

## 2026-10-26：主窗口功能区

1. **[DONE] 分区骨架。** `ShellWindow` 有布局工作台和编程工作台。停靠区是场景、程序、路径点、属性、关节点动、问题。直接运行会加载示例工位。`--verify` 退出码仍是 0。`olp_tests` 五十六项通过。点动不写入程序。
2. **随后完成**：中间三维视口已接上，见上一节。

## 2026-10-26：零位到接近点躲开前箱壁

1. **[DONE] 接近过渡。** `insertSafeTransitBeforeApproach` 在接近点正上方插入直线点 `CLEARANCE_1`，接近段改为直线下降。演示不再扫过 `Workpiece_BoxWall_Front`，退出码 0。`olp_tests` 五十二项通过。
2. **未完成，转入活跃计划**：主窗口功能区。回退段对前箱壁的间隙仍低于 5 mm。

## 2026-10-26：插补采样检查焊枪扫掠

1. **[DONE] 段内焊枪球。** 关节插补和直线插补的中间采样都做碰撞检查，最后一帧就是目标点，不再重复计。两端都干净、中途穿过夹具时会报 `Sweep collides with`。`olp_tests` 四十六项通过。
2. **未完成，转入活跃计划**：演示程序从零位到接近点的关节运动扫过 `Workpiece_BoxWall_Front`，退出码变为 2。

## 2026-10-26：平面场景改由 OCCT 导入

1. **[DONE] 焊缝边序。** 腹板和前箱壁的边集合与内置解析相同。第 3 条焊缝的端点、方向和角平分线一致。方向沿曲线参数；按面环反向会把起弧换到另一头。`importStep` 在有 OCCT 时先走 OCCT。`build-full` 的 `olp_tests` 四十四项通过，主程序仍把起点收到 32 mm 后退出码 0。
2. **未完成，转入活跃计划**：插补过程中的焊枪扫掠碰撞。视口、3D PDF 和 ROBOGUIDE 仍未开始。

## 2026-10-26：OCCT 读出每个实体的名字

1. **[DONE] OCCT 装配实体名。** `importStepWithOcct` 用 `MANIFOLD_SOLID_BREP` 的名字认每一块实体。整场景十二个名字（含 `FANUC_M10iA_Static_Base`）和 T 型接头四块板都对得上。实体名为空时，单实体文件退回产品名。`build-full` 的 `olp_tests` 三十八项通过。平面整场景仍先走内置解析。
2. **未完成，转入活跃计划**：对过边序后再决定平面场景是否改走 OCCT。ROBOGUIDE、客户实景和 3D PDF 熔断仍未开始。

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
