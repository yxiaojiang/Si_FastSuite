# 第一部分（阶段 1 骨架）Plan 对齐与框架审计报告

- 审计对象：`Si_FastSuite` 当前 `main`（提交 `4434da5`）
- 设计权威：`Docs/OLP_System_Design_Claude.md` v1.1（下称「指导文档」）
- 计划文本：根目录 `Plan.md` 与 `.claude/Plan.md`
- 规则与接手边界：`.claude/Rules.md`、`.claude/SessionBrief.md`、根目录 `CLAUDE.md`
- 审计日期：2026-10-25
- 审计方式：只读对照文档与源码，并在本环境用 g++ 13 实际配置、编译、运行 `olp_tests` 与 `SiFastSuiteOLP`。本环境 CMake 日志为 `Qt6 not found`、`OpenCASCADE not found`。未改任何业务代码。
- 本报告路径：`Docs/AUDIT_Part1_Plan_and_Framework.md`（仓库文档目录实际为 `Docs/`，不存在小写 `docs/`）

---

## A. 执行摘要

**Plan 对齐度：高，主体严格一致，存在少量部分偏离，计划本身没有把 P2 偷渡进 P0。**  
`Plan.md` 对指导文档的 MVP 范围、五层架构、M01~M15、阶段 0~5、3D PDF 熔断、方案 A、9 项验收，是逐条转写加周次展开。两份 Plan 正文实质相同。偏离集中在三处：验收样本用管法兰替换了指导文档的搭接焊、J2/J3 耦合系数在未经 ROBOGUIDE 对拍前被写成 `k23 = 1`、以及 Plan §2.3 把 12 实体场景写成「11 个实体」。

**框架健康度：不合格。能编译、20 条断言全绿，不能说明阶段 1 骨架正确。**  
五层 target 名称已经搭好，数据结构和命令栈的骨架方向与指导文档一致。真正落地的几何、运动学、视图、碰撞和导出，大量是与类名/注释同名的空壳或自洽闭环：`OcctCadKernel::importStep` 不调用 OCCT；`OpwKinematics` 在全零关节下把法兰放在 `(0, 0, 1790) mm`，同仓库 URDF 零位法兰在 `(890, 0, 1200) mm`，距离 **1067.8 mm**；Qt 视口函数体为空；HTML 导出是 2D canvas，没有 Three.js / GLB。

**是否可进入下一阶段：不可以。**  
下一阶段（指导文档阶段 2：焊缝、路径、报告与 HTML）会直接消费现在这套「已经绿了」的内核、正逆解和导出。在 P0 修掉之前继续堆功能，会把错误几何和错误运动学固化进路径与 `.ls`。

### A.1 「第一部分」在 Plan / 接手摘要中的定义，以及指导文档对应范围

三份文本对「第一部分」的口径并不相同，审计同时对照三者，避免用最窄或最宽的定义放过问题。

| 口径 | 位置 | 应实现范围 |
| :--- | :--- | :--- |
| 接手摘要里的阶段 1 | `SessionBrief.md` §1.4、§3.1 | CMake + C++20 + Qt6 + OpenCASCADE 五层骨架（`olp_core/cad/device/algo/io/app/ui` + `olp_tests`），并完成编译与单测。阶段 0 预研仍是「下一步」，尚未关闭。 |
| Plan 当前活跃事项 | `Plan.md` §一.2（标为 IN PROGRESS） | 在上述骨架上落地：M01 文档 + 命令栈 + **选择集 + 事件总线**；M02 `ICadKernel` + **OpenCASCADE 实现**；M03 三维视图 + **不挂接对象的 TcpGizmo**；M05/M06 `FANUC M-10iA` + OPW + J2/J3 耦合 + WPR；M12 `.ls` 生成器。 |
| 指导文档阶段 1 完成定义（M1） | 指导文档 §9.2「阶段 1：骨架（第 4~8 周）」 | 上表全部，再加：拾取视口、方案 A 机器人替换、焊枪/UFRAME、点动、简单播放、**工程保存/打开**、以及「导出 `.ls` 在 ROBOGUIDE 中跑出同样轨迹」。 |

仓库现状比 SessionBrief 的「骨架」写得更满（`main.cpp` 已串起导入、焊缝、仿真、`.ls`、HTML），比指导文档的 M1 又缺关键项（无 Qt 视口、无 OCCT、无工程文件、无 ROBOGUIDE 对拍）。README 的「20/20 全部通过」使用的是指导文档第 11 节的验收口吻。本报告把「第一部分」定为：**SessionBrief 的阶段 1 骨架 + Plan §一.2 点名的模块**；凡是代码里已经出现、但属于阶段 2~4 的实现，按「提前占位」审查，不按「阶段 2 已完成」给通过。

### A.2 状态、边界、分层（框架三项实质结论）

**状态与生命周期。** `CommandManager` 对「位姿修改」这一条路径是闭合的：拖动预览不入栈，松手一条命令，Esc 不入栈，连续 100 步 Undo/Redo 在 `tests/test_main.cpp` 的 Test 6 中实测通过。除此之外文档没有统一状态机。`WorkcellLayoutService::importFullSceneAndReplaceRobot`、`defineUFrameByThreePoints`、`src/main.cpp` 直接改 `ProjectDocument`。`defineUFrameByThreePoints` 返回 `SceneNode&`，指向 `vector` 元素；g++ 13 在 `src/main.cpp:51` 给出 `-Wdangling-reference`。`ModifyTargetPoseCommand` 持有 `ProjectDocument&`，命令对象的寿命绑在文档上，文档不能先于撤销栈销毁。派生缓存 `TargetCache::valid` 只在改位姿时清掉，没有事件总线在源数据变化后调度重算。

**边界。** P0/P1/P2 在 Plan 正文里守住了。代码边界没有守住：阶段 3 的碰撞/仿真、阶段 2 的焊缝与 HTML，都以可调用实现的形式放进了骨架，并用测试命名成「第 11 节验收」。算法层对 UI 的头文件依赖是干净的（`src/algo/*.cpp` 不包含 Qt）。模块对外的能力边界是脏的：`OcctCadKernel` 在 OCCT 缺失和 OCCT 存在时走同一套正则解析（`#ifdef OLP_HAS_OCCT` 只包住一组未使用的头文件）。`importStl` 把 ROS 网格按文件原始单位读入；`base_link.stl` 包围盒约 `[-0.142, -0.140, 0] ~ [0.143, 0.143, 0.278]`，单位是米，内部约定是毫米。

**分层。** CMake 的 7 个静态库与指导文档 L1~L5 的名字对齐，依赖方向大体是 L1→L2→L3→L4、L5 侧挂。三处分层已经偏了：

1. 指导文档把 M07 放在应用层，Plan 写在 `src/app/seam/`；实现放在 `olp_algo` 的 `SeamAndToolpathAlgo`，算法库因此必须链接 `olp_cad`。
2. `olp_ui` 的 `MainWindow` 直接持有具体类 `cad::OcctCadKernel`，表现层没有只依赖 `ICadKernel`。
3. `olp_tests` 链接的是 `olp_ui`（`CMakeLists.txt` 第 170 行），运动学测试无法在「不拉起 UI 目标」的前提下单独链接 `olp_algo`。指导文档「算法层 100% 可单测」在工程组织上还没有成立。

---

## B. Plan 对齐矩阵

判定只评价 **Plan 文本相对指导文档**。代码是否做到，放在「证据」列和 C 节，不把实现失败算成 Plan 越权。

两份 Plan：`diff Plan.md .claude/Plan.md` 只有两处。`.claude/Plan.md` 第五节标题用了「與」；阶段 3 第 17 周一句是「自适应插补段内碰撞检测」，根目录 `Plan.md` 同句是「自适应插补采样检测」。范围、模块、工期没有第二份计划。下文锚点用根目录 `Plan.md`。

| # | 条目 | 指导文档要求 | Plan 表述 | 判定 | 证据 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 目标闭环 | §0、文首：导入 → 工位 → 焊缝/路径 → 校验 → 仿真 → `.ls` + 3D PDF/HTML 保底 | 文首目标闭环与此相同，并写明单文件 HTML 为保底 | 严格一致 | `Plan.md` 第 5 行；指导文档第 3 行、第 21 条 |
| 2 | MVP 收窄 | §0.2、§3.1：1 个品牌 FANUC、1~2 款 6 轴、1 把焊枪、静止工件、角焊/搭接为主、L/J 为主、C 为 P1 | §3.1 把首款型号落成 `FANUC M-10iA / ARC Mate 100iC`，其余 P0 条目覆盖指导文档 §3.1 | 严格一致 | 型号落地来自 SessionBrief 对 `SampleScenes` 的拍板，指导文档 §3.4/§12 允许在预研第 1 周定型号 |
| 3 | P1 清单 | §3.2：C、投影/求交焊缝、自动避障、过渡路径、方案 B/C、USD/MP4、条件失败后的 3D PDF、参数模板 | §3.2 同清单，并写上 MakeTP 回显（指导文档 M12 已标 P1） | 严格一致 | `Plan.md` §3.2；指导文档 §3.2、M12「编译验证」 |
| 4 | P2 / 非目标 | §3.3：变位机、多机、寻位、多层多道、标定、读回 `.ls`、其他品牌、AI、Weld Cockpit | §3.3 逐条相同 | 严格一致 | `Plan.md` 第 98~99 行 |
| 5 | 3.4 方案 A/B/C | §3.4：默认方案 A；B/C 由预研决定，进 P0 则工期顺延 | §2.3、§3.1.2 拍板方案 A 为 P0；B/C 留在 §3.2 | 严格一致 | 拍板本身符合指导文档「预研第 1 周定方案」 |
| 6 | 方案 A 的实体数量 | §3.4 不规定个数；场景以文件为准 | §2.2 写 12 个 `MANIFOLD_SOLID_BREP`；§2.3 第 1 步写「解析出 11 个实体节点」 | 部分偏离 | 文件内实际 12 条 `MANIFOLD_SOLID_BREP`（`Full_Welding_Cell_With_Robot.step`）。Plan 自相矛盾。代码导入结果是 12，与 §2.2 一致 |
| 7 | 五层与模块边界 | §4.1：L1 表现 / L2 应用 / L3 算法 / L4 几何与设备 / L5 IO；算法不依赖 Qt | §4.1 映射到 `olp_ui/app/algo/core/cad/device/io`，并写明 `olp_algo` 禁止依赖 Qt | 严格一致 | 目录约定写得比指导文档更细（`src/app/seam/` 等），见第 28 行 |
| 8 | 四条架构铁律 | §4.1 设计原则：命令通道、派生数据可重算、`ICadKernel` 隔离、位姿相对 UFRAME、手改保留、拓扑签名 | §4.2 四条铁律与 §4.3 三条持久化规则，文字与指导文档同义 | 严格一致 | `Plan.md` §4.2 |
| 9 | 核心数据模型 | §4.3：Project / SceneNode 四类 / ControllerUnit / Program / Operation / Target 角色与缓存 / SeamDefinition | §4.1 `olp_core` 职责列表与 §五 M01 的 `ProjectDocument` 字段覆盖该树 | 严格一致 | 实现见 `include/olp/core/ProjectModel.h`，结构在场，行为不完整（C 节） |
| 10 | M01 应用框架 | §5 M01：主窗口、双工作台、文档、命令与合并、选择集、事件总线、后台任务、设置、日志；多语言 P1；Python P2 | §五 M01、阶段 1 的 4.1 周次覆盖到选择集、事件总线、后台任务；未把多语言/插件提前 | 严格一致 | 代码缺少 `SelectionManager`、`EventBus`、`TaskRunner` |
| 11 | M02 几何内核 | §5 M02：`ICadKernel`、XCAF STEP、颜色、单位毫米、拓扑、弧长、法向、粗细两套网格、签名 P0；IGES/ShapeFix/求交 P1 | §五 M02 接口列表与指导文档 P0 项对应，点名 `STEPCAFControl_Reader`、`TopExp`、`BRepExtrema_DistShapeShape`、跨零件 `< 1.0 mm` | 严格一致 | `< 1.0 mm` 比指导文档「最近点距离」更具体，与 SessionBrief 一致，不构成范围扩大。代码默认公差是 `2.0`（`ICadKernel.h`） |
| 12 | M03 视图与 TcpGizmo | §5 M03 五条：不 `Attach`、参考系与吸附、拖动只预览且工作线程只留最新帧、松手一条 `ModifyTargetPoseCommand`、多选宏命令、Esc 取消 | §五 M03 五条逐字对应；阶段 1 第 7 周单列 | 严格一致 | 代码只实现了「预览不写文档 / 松手一条命令 / 吸附 / 多选宏命令」的纯数学部分 |
| 13 | M04 工位与方案 A | §5 M04：场景树、放置、挂枪、分组、UFRAME 三点法、按名称或用户标记替换、基座对齐、初始关节可反算或手输 | §五 M04、阶段 1 第 6~7 周；§2.3 写对齐误差 `< 0.01 mm` | 部分偏离 | 指导文档验收第 7 条是基座偏差 `< 1 mm`。Plan 把 `< 0.01 mm` 写成执行路径，严于验收，方向一致。反算初始关节仍留在方案 A 描述中，与指导文档一致 |
| 14 | M05 设备模型 | §5 M05：JSON（OPW、限位、网格、Home、J2/J3 耦合）、1~2 款、工具与多 TCP P0；URDF/向导按 3.4 可升 P0，当前是 P1 | §五 M05 把 M-10iA 写成首款 JSON；方案 B/C 留在 P1 | 严格一致 | `resources/robots/fanuc_m10ia.json` 存在，但没有任何 `.cpp` 读取它 |
| 15 | M06 运动学 | §5 M06、§6.2~6.4：FK、8 组解析 IK、圈数、配置字、选解、焊接段禁止换配置、J2/J3 耦合（系数需 ROBOGUIDE 核定）、限位、三类奇异、点动；数值 IK 为 P1 | §五 M06 把耦合写成 `k23 ∈ {-1,0,+1}`、默认 `θ3_world = J2 + J3`，并给出万向锁分支 `w = 0` | 部分偏离 | 指导文档明确「符号和系数需在 ROBOGUIDE 中核对」，禁止在对拍前当事实。Plan 在阶段 0 的 S0-4 仍要求 20 组对拍，文本上保留了验证门，但默认公式已经写死 |
| 16 | M07 焊缝与 §6.1 | 选边/边链、同实体邻接 + 跨零件贴合、焊接侧、`X=t, Z0=-b, Y=Z0×X`、三角度、起止内缩；投影与求交为 P1 | §五 M07 公式与指导文档 §6.1 一致，并补充 `‖n1+n2‖ < 1e-5` 报错 | 严格一致 | 放置路径写成 `src/app/seam/`，与指导文档「M07 在应用层」一致 |
| 17 | M08/M09 路径与事件 | §5 M08/M09：步长、直线两点、切入切出、手改保留、程序树、Arc Start/End、FINE/CNT、默认事件规则；过渡路径与拆分合并为 P1 | §五 M08/M09 给出默认速度/CNT 表，与指导文档示例 `.ls` 一致 | 严格一致 | 代码已实现生成器，属阶段 2 范围，见 C 节越权 |
| 18 | M10 碰撞与 §6.5 | 碰撞对、焊丝尖端对**当前工件**豁免、FCL/Coal、离散 + 段内采样、两级间隙、可达性、问题列表、自转扫描；自动避障 P1 | §五 M10、阶段 3 周次与此对应；豁免半径写成 `8~15 mm` | 严格一致 | 半径是 Plan 的实现建议，指导文档只要求「一小段/允许穿透」。不扩大范围 |
| 19 | M11 仿真 | J 同步、L 直线+SLERP 且配置一致、梯形/S 曲线 P0、CNT 近似 P1、时间轴、StartState、节拍估算、帧烘焙 P0 | 阶段 3 的 6.2 与此对应，并写明节拍是估算 | 严格一致 | `Plan.md` §3.1.10、阶段 3 |
| 20 | M12 `.ls` | 段结构、UFRAME/UTOOL、WPR、配置字、关节输出要处理耦合、命名校验、预览 P0；多程序与 MakeTP 为 P1 | §五 M12 覆盖段结构与命名；完整关节输出放在阶段 4 | 严格一致 | 与指导文档「阶段 1 最简 `.ls`、阶段 4 做全」的切分一致 |
| 21 | M13 报告 / HTML / 3D PDF | 2D PDF 与单文件 HTML（Three.js + Base64 GLB）为 P0；3D PDF 预研 5 个工作日熔断，通过才进阶段 4 | §五 M13、§一.3、风险 R03/R04 复述熔断四条；HTML 工期 1.5~2 周 | 严格一致 | 指导文档 §0.6、M13、§9.2 阶段 0 |
| 22 | M14 工程文件 | zip + `project.json` + CAD 副本 + `.brep` + 网格、版本迁移、自动保存 P0 | 阶段 1 第 8 周「M14 基础版」；完整自动保存写在 §3.1.13 | 严格一致 | 仓库中没有 `src/io/project/`，没有保存/打开实现 |
| 23 | M15 测试与 CI | GoogleTest、10~20 回归场景、每次提交编译+测试、安装包 P0；授权 P2 | §五 M14&M15、阶段 5 安装包；单测框架写 GoogleTest | 严格一致 | 代码是 `tests/test_main.cpp` 手写 `check()`，无 `.github/` CI，无安装包。授权保持 P2 |
| 24 | 技术选型 | §7：C++17/20、Qt6、OCCT 7.8+、Eigen、自研 OPW、FCL/Coal、nlohmann/json、QPdfWriter、three.js+tinygltf、GoogleTest、CMake+vcpkg；usd-brep 不用 | §4.1 依赖列与此相同；全文未把 usd-brep 列入 MVP | 严格一致 | `vcpkg.json` 列出 eigen3、nlohmann-json、opencascade、qtbase、fcl、gtest；CMake 未接 vcpkg toolchain，源码未 include 这些库 |
| 25 | 阶段 0 | §9.2：OCCT 拾取、20 组 ROBOGUIDE 对拍、金样本 `.ls`、方案决策、3D PDF 5 天、HTML 1~2 天、Gizmo、FCL 耗时、CI | §六阶段 0 的 S0-1~S0-8 覆盖上述项；S0-1 标为 2026-10-25 完成；S0-2 把骨架提前到第 1 周 | 严格一致 | 指导文档阶段 0 交付物含「代码仓库与 CI」。把骨架放到 S0-2 是排期细化，没有删预研项 |
| 26 | 阶段 1 / M1 | §9.2 阶段 1 与里程碑 M1（第 8 周端到端，`.ls` 进 ROBOGUIDE） | §六阶段 1 按三条人员主线拆周，M1 文字与指导文档同义 | 严格一致 | Plan §一却把阶段 0 与阶段 1 骨架并成一条 IN PROGRESS，和 §六「阶段 0 尚未做完 S0-3~S0-8」并列。这是计划内部的状态标记过宽，不是范围越权 |
| 27 | 阶段 2 / M2 | 第 9~14 周：焊缝、路径编辑、工艺事件、2D PDF、HTML 基础版；碰撞只做点检测 | §六阶段 2 与 M2 验收场景写成 T 型件 + 整工位箱体 | 严格一致 | 场景名落到 `SampleScenes`，仍在指导文档「T 型角焊」演示范围内 |
| 28 | 阶段 3 | 第 15~19 周：碰撞矩阵、段内检测、问题列表、自转扫描、J/L、时间轴、节拍、烘焙 | §六阶段 3 同内容 | 严格一致 | 根目录与 `.claude/Plan.md` 在第 17 周用词不同，都仍是段内检测 |
| 29 | 阶段 4 | 第 20~23 周：完整 `.ls`、HTML 完善、3D PDF 仅在预研通过或购买 SDK 时做，否则 2 周转给遗留/P1 | §六阶段 4 包含熔断后的去向 | 严格一致 | 指导文档 §9.2 阶段 4 |
| 30 | 阶段 5 / M3 | 第 24~28 周：ROBOGUIDE 回归、真机空走与实焊、性能、自动保存、安装包 | §六阶段 5 相同，并点名管法兰、方管、箱体 | 严格一致 | 指导文档 §9.2 阶段 5 |
| 31 | 团队与工期 | §9.1：4 名研发 + 0.3 顾问；§9.3 弹性（2 人则加长、方案 B +3 周、方案 C 每种格式 +1.5~2 周） | §六开头复述同一团队与 M1/M2/M3 周次；方案 B/C 的工期在 §3.4 的 Plan 转写里保留在 §二/风险，未另写一节弹性 | 部分偏离 | 指导文档 §9.3「2 人约 10~12 个月」未进入 Plan。不影响模块范围，后续排期若减人会缺依据 |
| 32 | 风险清单 | §10：无 ROBOGUIDE、参数不一致、3D PDF、Reader 关 3D、场景范围、Gizmo、STEP 质量、焊缝识别、碰撞性能、CNT 节拍、撤销后补、范围蔓延 | R01~R09 覆盖其中 9 项 | 部分偏离 | 缺少独立条目：CNT/节拍与真机差异（指导文档 §10 倒数第 3 行）、`ShapeFix` 与法向翻转。节拍「是估算」写在 §3.1.10 正文，风险表未单列 |
| 33 | 9 项验收 | §11：运动学对 ROBOGUIDE 100 组；`.ls` 可加载；三类焊缝姿态 `< 5°`；10 个碰撞场景；30 FPS / 一条撤销；PDF+HTML（+条件 3D PDF）；场景替换 `< 1 mm`；2 小时与 100 步撤销与工程重开；真机 | §八 9 条结构相同 | 部分偏离 | 第 3 条：指导文档是「T 型角焊、搭接、箱体内角」；Plan 写成「T 型、管法兰、箱体内角」，搭接焊从验收样本中消失。第 7 条在 `< 1 mm` 后追加「实测目标 `< 0.01 mm`」，严于指导文档，可接受为内控 |
| 34 | TcpGizmo / 熔断 / 方案 A 的 v1.1 修订 | §13：三条复审全部吸收，总工期仍 28 周 | Plan 文首声明遵循 v1.1，三条都进入 P0/阶段 0/阶段 1 | 严格一致 | 未见把 v1.0 已否决的「裸用 Attach」或「先做 3D PDF」写回计划 |
| 35 | 记忆文件形态 | `.claude/Rules.md`：`Plan.md` 只保留当前 1~3 个活跃事项，完成项进 `Plan-archive.md` | 两份 `Plan.md` 都是 386 行全量计划；§一仍有 3 条活跃事项 | 部分偏离 | 相对 Rules，不相对指导文档。全量计划与 Rules 的「10% 工作记忆」冲突。本次审计不改 Plan |

**模块覆盖结论：** M01~M15、阶段 0~5、P0/P1/P2 均已在矩阵中给出判定。没有「指导文档有、Plan 完全没写」的 P0 模块。没有把变位机、多机器人、读回 `.ls`、其他品牌写进 P0 的越权。

**计划内部还需要知道的一点：** §一.2 把 M06 全套 OPW 和 M12 生成器放进「正在做的骨架」，§六又把它们分别放在阶段 0 的 S0-4/S0-5 和阶段 1 的第 6~8 周。这不改变范围，但会让「第一部分做完了什么」无法从计划状态栏读出来。README 已经按「闭环已通过」来写，和 §一的 IN PROGRESS 矛盾。

---

## C. 框架问题清单

严重度：P0 = 进入下一阶段前必须改，否则后续模块会建立在错误结果上；P1 = 骨架里已经写错或与契约不符，下一阶段会踩到；P2 = 边界债，可在对应阶段修。

| ID | 严重度 | 位置 | 现象 | 为何违反指导或逻辑 | 建议 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| F01 | P0 | `src/cad/OcctCadKernel.cpp` `OcctCadKernel::importStep`（约第 63~384 行）；头文件 `include/olp/cad/OcctCadKernel.h` | 类名和 CMake 文案是 OpenCASCADE 内核。`#ifdef OLP_HAS_OCCT` 只包含 `<STEPCAFControl_Reader.hxx>` 等头，函数体内没有任何 OCCT 类型或调用。导入是按行正则匹配 `CARTESIAN_POINT` / `EDGE_CURVE` / `ADVANCED_FACE` / `MANIFOLD_SOLID_BREP`。`tessellate`（第 541~552 行）对任意实体返回包围盒 12 三角面的盒子。非 `PLANE` 的面被标成 `Cylinder` 且法向保持默认零向量。颜色、装配变换、`TopAbs_REVERSED`、弧长曲线都没有实现。 | 指导文档 §7、M02、阶段 0/1 的几何权威是 OCCT XCAF。本仓库样例 STEP 由 `StepBrepBuilder` 生成且一行一个实体（`Full_Welding_Cell_With_Robot.step` 文件头），正则解析能数出 12 个盒子，所以 Test 4 通过。工业 STEP 按 72 列折行、含裁剪曲面和装配坐标系时，这条路径会静默丢实体或把坐标读错。OCCT 安装与否不改变这条路径。 | 有 OCCT 时走 `STEPCAFControl_Reader` + `TopExp` + `BRepMesh_IncrementalMesh`，接口保持 `ICadKernel`。没有 OCCT 时 `importStep` 应失败并说明原因，而不是返回「成功导入 B-Rep」。盒子网格不要冒充碰撞网格。 |
| F02 | P0 | `src/algo/OpwKinematics.cpp` `forwardKinematics` / `inverseKinematics`；`src/device/RobotModel.cpp` 第 24~25 行；`fanuc_m10ia_standalone.urdf` 第 35~65 行 | 零位 FK 法兰 = `(0, 0, 1790) mm`。URDF 关节链在 q=0 的 link_6 原点 = `(890, 0, 1200) mm`（0.150+0.640+0.100 m 沿 X，0.450+0.600+0.150 m 沿 Z）。距离 1067.8 mm。Home（J5=−90°）FK 法兰 = `(−100, 0, 1690) mm`。`RobotModel::createFanucM10iA` 写入 `offsetsRad = {0,0,−π/2,0,0,π}` 与 `signCorrections = {1,1,−1,−1,−1,−1}`，`OpwKinematics` 从未读取。URDF 的 J3/J4/J5/J6 轴是 `(0,−1,0)`、`(−1,0,0)`、`(0,−1,0)`、`(−1,0,0)`，FK 使用的是 `RotZ * RotY * RotZ` 腕部。`expandTurns` 只展开 J6。 | 指导文档 §6.2 / 验收第 1 条要求与 ROBOGUIDE 同型号一致，误差 `< 0.01 mm`。当前 IK 只是同一套错误 FK 的反函数。Test 3（`tests/test_main.cpp` `testOpwKinematics100Samples`）测的是 `FK(IK(FK(q)))`，实测 `maxPosErr=0.000000 mm`，与 URDF 无关。`loadFromUrdf` 也不解析几何，只覆盖关节限位后仍用硬编码 OPW，因此 Test 2 检查 `opw.c2 == 600` 不能证明 URDF 里有这个参数（数值来自 `createFanucM10iA`，URDF 里只是注释）。JSON（`resources/robots/fanuc_m10ia.json`）的 `joint_offsets_deg` 全是 0，和 C++ 里的 −90°/180° 又不一致，且 JSON 无读取代码。 | 以 URDF 关节原点为金样本写 FK 测试：至少断言 q=0 时法兰位置误差 `< 0.01 mm`，再谈 8 组解和耦合。`signCorrections` 与 `offsetsRad` 要么进入求解，要么从模型里删掉，避免两套「真参数」。在 ROBOGUIDE 20 组点之前，不要把耦合系数写成已核定。 |
| F03 | P0 | `src/cad/OcctCadKernel.cpp` `importStl` 第 386~435 行，注释在第 419 行 | 注释写「若包围盒小于 10 则从米换到毫米」。循环里把 `float` 顶点原样写入 `TriMesh`，没有尺度判断，也没有毫米换算。本仓库 `base_link.stl` 为二进制 412 三角面，包围盒约 0.28 m 高。Test 2 只断言三角形非空。 | 指导文档 §2.3 第 6 点与 M02「单位统一」：URDF/STL 默认米，内部必须是毫米。差 1000 倍的网格一旦进入显示或碰撞，就是系统性错位，测试仍绿。 | 按 URDF 约定把 `< 10` 的网格乘 1000，或在导入 API 上强制传入单位并在机器人加载器里写死米→毫米。测试应断言 `base_link` 高度约 278 mm，而不是「有三角形」。 |
| F04 | P0 | `src/app/WorkcellLayoutService.cpp`；`src/main.cpp`；`include/olp/app/CommandManager.h`；`src/ui/MainWindow.cpp`；`src/ui/OcctViewport.cpp` | 位姿命令栈存在，但导入场景、隐藏静态机器人、放置机器人、写 UFRAME、`main` 里 `doc.seams.push_back` / `doc.controllers.push_back` 全部直接改文档。仓库中不存在 `SelectionManager`、`EventBus`、`TaskRunner`。`MainWindow` 不是 `QWidget`，没有 `QApplication`。`OcctViewport::fitAll` 函数体只有一行注释。`OLP_HAS_QT6` 只负责链接 Qt，没有任何窗口代码。 | 指导文档 §2.3 第 1 点与 §4.1：撤销必须在第一阶段成为唯一写入口，后期补等于重写编辑器。Plan §一.2 把选择集和事件总线列入第一部分。现在业务主路径已经在命令系统旁边长出来了。 | 第一部分收口标准改成：所有 `ProjectDocument` 写入都是 `ICommand`；视口在没有 Qt/OCCT 时保持明确的「未启用」，不要用空函数表示「视图基础已完成」。选择集与变更通知至少有能单测的空实现被强制走通，再开始阶段 2 的编辑器。 |
| F05 | P0 | `src/algo/CollisionAndSim.cpp` `checkTorchPoseCollision` 第 68~74 行 | 豁免条件是 `node.id == activeWorkpieceId \|\| node.collisionRole == Workpiece`。只要角色是工件，焊丝尖端球就跳过，与传入的 `activeWorkpieceId` 无关。检测体是沿 TCP −Z 的 4 个球，相对 `collisionMesh` 的 AABB 做有符号距离。机器人连杆不参与。`OLP_HAS_FCL` 在该 cpp 中无引用。`UnitsSettings::wireTipExemptionRadiusMm` 无读取。段内采样循环（第 237 行起）只烘焙关节，不对中间点做碰撞。 | 指导文档 M10 原文是对「当前焊接的工件」豁免，用来避免焊缝根部全红；邻件、夹具、连杆仍然要检。当前条件会把第二块工件上的真实干涉放掉。Test 5 把 TCP 放进夹具 AABB 就判定「检出夹具碰撞」，AABB 包含测试点时必然为真，不能代表网格碰撞。 | 豁免只保留 `id == activeWorkpieceId`（以及明确的喷嘴白名单）。碰撞网格用三角面或 FCL，并使用 `SceneNode::localTransform`。段内检测若未做，函数名和测试名不要写成 M10 已完成。 |
| F06 | P0 | `tests/test_main.cpp`；`README.md` §4；`src/io/HtmlAnimationExporter.cpp`；`Tools/Compile.bat` | 本次实测 `olp_tests`：**20 passed, 0 failed**。这些断言的名称引用指导文档第 11 节，测量的却是另一件事：Test 3 是自反函数误差；Test 4 的 0.000 mm 是「静态底座 AABB 底面中心」相对 `(0,0,500)`，而该合成底座盒子正好 z∈[500,720]、xy 对称；Test 7 只做子串查找，且被测程序的 `config` 是结构体默认值 `'N U T, 0, 0, 0'`，没有跑 IK。README 写 Three.js + Base64 GLB、ROBOGUIDE 金样本、姿态误差 `< 0.001°`。实际 HTML（本次生成 83962 字节）是 `<canvas>` 2D 折线，`sceneParts` 写入后 `render()` 未使用，文件中无 `THREE` / `gltf` / `GLB`。`Compile.bat` 在 ctest 失败时打印 WARNING，最终仍 `exit /b 0`。`main` 不检查 `exportToFile` 返回值；输出目录不存在时仍然打印「Exported」并返回 0（本次先缺少 `build/` 时即如此）。 | 指导文档 §11 是外部金样本（ROBOGUIDE、真机、Acrobat/浏览器里的真实动画）。用内部闭环填满同名测试，会使阶段门禁失效。这是第一部分能否被信任的问题，不只是文档措辞。 | 测试名改成它真正测量的命题。验收第 1、2、6 条在拿到外部样本前保持失败或标成「未执行」。构建脚本在测试失败时返回非零。导出失败要传到进程退出码。 |

### P1

| ID | 严重度 | 位置 | 现象 | 为何违反指导或逻辑 | 建议 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| F07 | P1 | `src/ui/TcpGizmo.cpp` `updateDragDelta` 第 55~63 行；`include/olp/ui/TcpGizmo.h` | `Tool` 与 `Seam` 都是右乘；`World` 与 `UFrame` 都是把平移加到 `poseInUFrame.pos` 上。预览回调不计算 IK、不检测碰撞，`GizmoPreviewState::ikReachable` 保持默认 `true`。没有工作线程，没有 `AIS_Manipulator::StartTransform/Transform/StopTransform`。 | 指导文档 M03：世界系增量必须换到 UFRAME 再作用于「存在 UFRAME 下的位姿」。UFRAME 相对世界有旋转时，World 模式会把世界位移直接加进用户坐标。拖动期的逆解/碰撞是阶段 1 TcpGizmo 的验收内容（30 FPS 那条还没到，但参考系公式现在就是错的）。 | 为 World 传入 `uframeInWorld`，把增量变到 UFRAME 后再乘。Seam 使用焊缝坐标系，不要复用工具系。预览回调里接 `OpwKinematics`，文档仍然只在 `commitDrag` 写入。 |
| F08 | P1 | `src/algo/SeamAndToolpathAlgo.cpp` `generateOperationTargets` 第 252~260 行；`buildSeamFromPickedEdge` 第 124 行起 | 一条 `userModified` 记录按 `abs(ratio - arcLengthRatio) < 0.05` 匹配，匹配成功后 `savedMods` 不删除，相邻点会重复吃到同一偏移。采样帧只有 1 个时，该点同时是 first 和 last，分支只打上 `ArcStart`，没有 `ArcEnd`。跨零件第二面由 `findClosestMatingFaceAcrossParts` 给出，默认公差 2.0 mm（`ICadKernel.h` 第 70 行），Plan 写的是 `< 1.0 mm`。`pointToFaceBoxDistance` 用 `max(平面距离, 包围盒外距离)`，不是点到面的距离，贴合面在边界外 1 mm 内可能被丢掉。 | 指导文档 §4.3：重生成必须按弧长保留手改，不能把一个手改复制到多个点，也不能悄悄丢掉收弧。M09 默认规则要求焊缝终点带 `Arc End`。 | 每条手改最多匹配一个新点，并比较残余误差。单点焊缝同时写起弧和收弧，或拒绝生成并返回错误。公差与距离函数改成与 Plan/指导文档一致的点到面距离。 |
| F09 | P1 | `src/io/FanucLsPostProcessor.cpp`；`include/olp/io/FanucLsPostProcessor.h` `appendReturnHome` | 段结构（`/PROG` `/ATTR` `/APPL` `/MN` `/POS` `/END`）、`UFRAME_NUM`、`Arc Start/End`、`WELD_SPEED` 在本次运行的 `WELD_001.ls` 里存在，这一点是真实的。`LsPostConfig::appendReturnHome` 默认为 `true`，生成器不读它，输出里没有回 Home。`MotionType::C` 被写成 `L`。J 点没有关节坐标，因此也不做 J2/J3 耦合后的关节输出。配置字来自 `Target::config`；仿真没跑过就是默认 `'N U T, 0, 0, 0'`（Test 7 测的就是这个默认值）。本次主程序跑完仿真后四点都是 `'F U T, 0, 0, 0'`，该字符串来自有缺陷的 IK。 | 指导文档 M12：最简 `.ls` 也要能被控制器/ROBOGUIDE 接受；配置字必须来自求解；回 Home 是示例程序的一部分。死配置开关会让调用方以为已经回了 Home。 | 配置字只允许从已求解缓存写出，缺缓存则导出失败。`appendReturnHome` 为真时真正追加运动。在金样本文件到位前，用固定字符串对比测试代替「包含子串即通过」。 |
| F10 | P1 | `src/algo/CollisionAndSim.cpp` `validateAndSimulateProgram` 第 157~163、223~232、259 行 | `ArcStart` 在该点运动段之前就把 `arcOn` 设为真，所以进入 `WELD_START` 的那一段被计入焊接时间。本次主程序：总节拍 37.339 s，焊接 35.225 s，空走 2.114 s；其中约 0.225 s 是进枪段被算进焊接（焊缝长约 350 mm、`weldSpeedMmPerSec` 默认 10）。L 段「配置跳变」用 `weightedJointDistance > deg2rad(25)` 比较；该距离是加权范数，不是单轴最大跳变。J 插补是参数 `alpha` 的线性插值，没有梯形/S 曲线。限位失败的解在 `selectBestSolution` 里被丢掉，上层一律记成 `Unreachable` 或 `ConfigJump`。 | 指导文档 M11：节拍要分焊接/空走；§6.3 的阈值是相邻采样关节变化；超限与不可达是两种问题状态（`TargetValidationState` 里已经有 `JointLimitExceeded`，没有被赋值）。 | 工艺事件的生效时刻定在到达该点之后。跳变阈值改成各轴最大 `|Δq|` 或单独的腕部条件。速度曲线和问题分类留到阶段 3 时按枚举写全，不要在骨架里把超限报成不可达。 |
| F11 | P1 | `src/app/WorkcellLayoutService.cpp` 第 22~40 行；`src/main.cpp:51` | 替换用静态底座 AABB 的 xy 中心和 `bboxMin.z`，不读取面 `Robot_Pedestal_Base_Top`，不对齐姿态，不从静态连杆反算关节，机器人直接放 Home。对本合成盒子，结果正好是 `(0, 0, 500)`，Test 4 误差 0。`defineUFrameByThreePoints` 返回 vector 元素引用；随后若再 `sceneNodes.push_back` 会失效。g++ 对 `main.cpp:51` 的 `-Wdangling-reference` 已复现。导入不检查第二次调用，场景节点会追加。 | 指导文档方案 A：拾取安装面、对齐位姿、关节非零时要反算或手输。现在的 0 mm 是资产和 AABB 碰巧同心，不是安装面算法。返回悬空引用是生命周期缺陷。 | 用底座顶面的平面原点和法向构造基座变换。函数改返回 id 或 `std::optional`。重复导入要么拒绝要么先清场景，并且走命令。 |
| F12 | P1 | `include/olp/core/ProjectModel.h` `SceneNode`；`OcctCadKernel::computeEdgeSignature` | 场景节点没有颜色字段。`TopologicalSignature::cadFileHash` 没有任何赋值。`parentId` 导入时为空，装配树没有建立。边被当成直线：`evaluateEdgeAtArcLength` 在端点之间线性内插，`isLinear` 恒为 true。 | 指导文档 M02 P0：保留颜色、单位、拓扑签名（文件哈希 + 几何签名）。管法兰/圆角焊缝（样例里已经有 `Workpiece_PipeFlange_DN15.step`、`Workpiece_U_Bracket_L70.step`）在直线边上会算错切向。 | 签名至少写入文件哈希；曲线边在 OCCT 实现里用 `GCPnts_AbscissaPoint`。颜色进入 `SceneNode`。这些可以随 F01 一起做，不要在正则解析器上补。 |
| F13 | P1 | `CMakeLists.txt`；`vcpkg.json`；`tests/test_main.cpp` | `find_package` 全部 `QUIET`，失败后继续。Eigen 只在找到时链接，源码未包含 Eigen。nlohmann/json、gtest、fcl 均未被源码使用。`olp_cad` 在找到 OCCT 时 `PUBLIC` 链接 OCCT，头文件隔离会被依赖方继承。测试目标依赖 `olp_ui`。无 CI。 | 指导文档 §4.1：上层只看见 `ICadKernel`。§7：GoogleTest + vcpkg。Plan §4.1 写 `olp_core` 依赖 Eigen 与 nlohmann。当前工程是「可选依赖的空宏」。 | toolchain 固定 vcpkg。OCCT 头只留在 `OcctCadKernel.cpp`。`olp_tests` 直接链接 `olp_algo` 与 `olp_cad`。gtest 替换手写 `check()` 可以放到修测试命题时一起做。 |
| F14 | P1 | `src/algo/OpwKinematics.cpp` `checkSingularity` 第 294~303 行；`computeConfig` | 奇异判据把 `|sin(q5)|` 和 `|sin(q3+ψ)|` 与 `deg2rad(3°)`（约 0.052）比较。对小角度，`|sin(q)|≈|q|` 碰巧接近「3° 以内」，但量纲不是角度，肩部奇异（腕心落在 J1 轴附近）没有检测。圈数展开不含 J1/J4。配置字 F/N、U/D、T/B 的几何定义没有外部样本。 | 指导文档 M06 要求腕、肘、肩三类奇异，以及 J1/J4/J6 圈数。肩部漏检会在机器人伸到基座轴线时给出「可解且非奇异」。 | 肩部用腕心到 J1 轴的水平距离。奇异阈值用角度或雅可比最小奇异值，并写明单位。配置字对照 ROBOGUIDE 再冻结。 |
| F15 | P1 | `src/app/CommandManager.cpp` `ModifyTargetPoseCommand::execute`；`ReverseOperationPathCommand` | 目标 id 不存在时 `execute` 直接 return，命令仍被压栈，Undo 变成空操作。`mergeWith` 在所有现有调用里因 `allowMerge=false` 不会发生，滑块合并等于没有。`isDirty` 在 Undo 后仍为 true。路径反向会把 `arcLengthRatio` 改成 `1 - ratio`，Approach/Retract 默认 ratio 0 会变成 1。 | 命令栈是阶段 1 唯一可信的状态机。空命令和脏标记会使「100 步一致」在更复杂编辑中失效。反向路径是阶段 2 功能，但代码已经在库里。 | 执行失败不要入栈。脏标记由命令声明是否使文档偏离上次保存点。反向只交换焊接点的弧长参数。 |

### P2

| ID | 严重度 | 位置 | 现象 | 建议 |
| :--- | :--- | :--- | :--- | :--- |
| F16 | P2 | `src/cad/OcctCadKernel.cpp` 解析循环 | `std::stod` / `std::stoi` 在坏数字上抛异常，导入函数没有捕获，进程直接终止。 | 捕获后返回 `false` 并带错误行号。 |
| F17 | P2 | `CollisionAndSimEngine` 构造函数保存 `const OpwKinematics&` | 调用方必须保证运动学对象比引擎长寿。`main` 里目前满足，类型系统不保证。 | 改为值或 `shared_ptr`，或在注释和测试里固定寿命。 |
| F18 | P2 | `HtmlAnimationExporter` 把 `node.name` 拼进 JS 字符串 | 名称含引号或 `</script>` 时页面破损。当前样例名称安全。 | 做 JSON 转义。在换成真正的 GLB 导出时一起处理。 |
| F19 | P2 | Plan 目录约定 vs 源码树 | Plan 写 `src/ui/viewport/`、`src/algo/kinematics/`、`src/io/post/` 等；源码是扁平的 `src/ui`、`src/algo`、`src/io`。 | 阶段 2 开工前选定一种目录并改 Plan 或改树，避免两套路径同时被人引用。 |
| F20 | P2 | `.claude/Rules.md` 与两份 Plan 的形态 | Rules 要求 `Plan.md` 只留 1~3 个活跃项；现在两份都是全量 28 周计划。`.claude/Plan.md` 有一个「與」字。 | 全量计划可留在 `Docs/` 或 `Plan-archive` 的索引策略里，按 Rules 把 `.claude/Plan.md` 收成活跃三条。本次按任务要求未改 Plan。 |

### 越权扩展（相对「第一部分」）与伪实现对照

下面这些类型已经出现在骨架里。它们相对指导文档的阶段归属是阶段 2~4。问题不是「写早了」本身，而是以完整实现的名字通过了测试。

| 代码 | 指导文档阶段 | 实际完成度 |
| :--- | :--- | :--- |
| `SeamAndToolpathAlgo::computeSeamFrame` / `applyWeldingAngles` | 阶段 2，§6.1 | 纯函数与公式一致（在法向输入正确的前提下） |
| `buildSeamFromPickedEdge` / `generateOperationTargets` | 阶段 2 | 只对「一行一个实体的平面盒子 STEP」跑通；曲线、边链、焊接侧选择、持久化签名未做 |
| `CollisionAndSimEngine` | 阶段 3 | 球 vs AABB，豁免条件错误，无段内碰撞，无 FCL |
| `HtmlAnimationExporter` | 阶段 2（P0 保底是 Three.js+GLB） | 2D canvas 折线 |
| 2D PDF、工程 zip、问题列表面板、点动面板、3D PDF | 阶段 1 末 / 阶段 2 / 阶段 4 | 仓库中不存在对应实现。`Qt6::PrintSupport` 已在 CMake 中链接，无 `QPdfWriter` 调用 |

---

## D. 做得好的地方

- **Plan 对 v1.1 的转写是认真的。** P2 清单、3D PDF 五天熔断、HTML 保底、方案 A/B/C 的工期门禁、算法层禁止依赖 Qt，都还在计划里，没有被骨架代码的「提前实现」改写掉。
- **数据模型的字段和指导文档 §4.3 对得上。** `TargetRole`、`ProcessEvent`、`poseInUFrame`、`userModified`、`TargetCache`、`FanucConfig` 都在 `ProjectModel.h`，后面换真实内核时不必先推翻文档结构。
- **WPR 与万向锁公式按 §6.4 实现，并且测试测的是矩阵往返。** `Transform4d::toWPR` 在 `sy < 1e-7` 时固定 `w = 0` 再用 `atan2(-r12, r22)`。Test 1 实测通过。这是第一部分里少数「测试命题 = 实现命题」的点。
- **位姿命令栈的主路径是对的。** `TcpGizmo` 在 20 次 `updateDragDelta` 期间 `undoCount()==0` 且文档坐标不变；`commitDrag` 产生 1 条命令并做 1 mm 吸附；`cancelDrag` 不入栈；100 次 Undo/Redo 恢复同一 `z`。`MacroCommand::undo` 按子命令逆序执行。这符合 M03「松手才提交」里已经能脱离视图验证的那一部分。
- **焊缝坐标系纯函数的构造顺序与 §6.1 一致：** 单位切向、`b = normalize(n1+n2)`、和向量过小则返回空、`Z0 = -b`、再正交化。T 型合成件上 Test 5 的角平分线误差约 `0.000001°`（在该正则解析器给出的平面法向下）。
- **样例资产和方案 A 的命名约定是可复现的。** `Full_Welding_Cell_With_Robot.step` 含 12 个命名实体、5 个 `FANUC_M10iA_Static_*`。`importFullSceneAndReplaceRobot` 能隐藏这 5 个并把机器人放到 `(0, 0, 500)`。`.ls` 分段和 Arc Start/End 的文本骨架与指导文档示例同构。
- **CMake 分层目标的名字和「算法源文件不包含 Qt」这件事，给后续替换内核留了接口。** `ICadKernel` 的虚函数列表已经覆盖 M02 要的导入、邻接、弧长、法向、离散、签名。替换 OCCT 实现时不必先改上层函数签名。

---

## E. 进入下一阶段前必须先修的清单

下一阶段指指导文档阶段 2（焊缝定义、路径编辑、2D 报告与 HTML 动画包），以及 Plan 里尚未关闭的阶段 0 对拍（S0-3~S0-8）。下面各项未完成时，不建议开始新的工艺功能。

1. **几何内核二选一，并让失败可见（F01、F03、F12）。** 接上 OCCT 的 STEP/STL/拓扑/网格；或者在 OCCT 不可用时让导入失败。删除「正则解析即 B-Rep 成功」和「STL 注释里写了换算、代码不换算」。
2. **运动学改到与本仓库 URDF 一致，并换掉自反测试（F02、F14）。** 最小门禁：q=0 法兰位置与 URDF 链相差 `< 0.01 mm`；`offsetsRad` / `signCorrections` 的处理方式唯一。ROBOGUIDE 100 组仍是阶段 0/5 的外部门禁，不在这一条里假装完成。
3. **把文档写入收口到命令，并补上 Plan §一.2 已点名的选择集与变更通知（F04、F15）。** `WorkcellLayoutService` 和 `main` 的直接改写要停。`defineUFrameByThreePoints` 不再返回 vector 引用。视口在 Qt/OCCT 缺失时不要呈现为已实现。
4. **若碰撞函数继续留在库里，先修正豁免谓词（F05）。** 只豁免当前 `activeWorkpieceId`。在 FCL 和段内检测完成前，测试名称改为「球与 AABB」。
5. **测试与 README 和真实行为对齐（F06、F09）。** 20 条绿断言可以保留为回归，但命题改成内部不变量。指导文档第 11 节的 9 条验收单独列表，未执行的保持未执行。`Compile.bat` 与 `main` 在失败时返回非零。
6. **TcpGizmo 的世界系公式（F07）在做任何交互编辑前改完。** 这是阶段 2 路径拖拽的入口，现在 World 与 UFrame 共用一套加法。
7. **阶段 0 仍缺的外部对拍不要并进「骨架已通过」。** 指导文档阶段 0：真实 OCCT 拾取、ROBOGUIDE 20 组、金样本 `.ls` 加载、3D PDF 5 天熔断记录、FCL 单次耗时。Plan §一.3 把这些标为 NEXT，与代码现状相符，应继续保持「未完成」。

修完 1~5 之后，骨架才适合作为阶段 2 的基线。第 6 条在第一条焊缝拖拽进入实现时同时完成。第 7 条是并行的预研门禁，不替代 1~5。

---

## 附：本次复现记录

环境：Linux，g++ 13.3，CMake 3.28，配置命令使用 `-DCMAKE_CXX_COMPILER=/usr/bin/g++`。未安装 Qt6 与 OpenCASCADE。CMake 状态行：

- `[SiFastSuiteOLP] Qt6 not found in current environment - building headless/stub UI layer`
- `[SiFastSuiteOLP] OpenCASCADE not found in current environment - using built-in STEP/STL B-Rep kernel adapter`

`olp_tests` 退出码 0，输出 `Summary: 20 passed, 0 failed.`  
`SiFastSuiteOLP` 在创建 `build/` 目录后退出码 0：导入 12 实体，隐藏 5 个静态机器人，基座 `(0, 0, 500)`，烘焙 748 帧，节拍 37.3394 s（焊接 35.225 s / 空走 2.11438 s）。生成的 `.ls` 含 `/PROG` 到 `/END` 与 `Arc Start[1]` / `Arc End[1]`，无回 Home 行。HTML 无 Three.js。  
另编译了一个只调用 `OpwKinematics::forwardKinematics` 的探针，得到零位法兰 `(0, 0, 1790)` 与 URDF 零位 `(890, 0, 1200)` 的差 1067.8 mm。

编译告警：`src/main.cpp:51` `possibly dangling reference to a temporary`（`defineUFrameByThreePoints` 的返回值）。
