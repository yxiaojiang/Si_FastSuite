# 第一部分（阶段 1 骨架）独立重审报告 —— Opus 独立审计

- 审计对象：`main` 分支提交 `4434da5`（仓库唯一提交）
- 设计权威：`Docs/OLP_System_Design_Claude.md` v1.1（下称「指导文档」）
- 计划文本：根目录 `Plan.md` 与 `.claude/Plan.md`；接手口径：`CLAUDE.md`、`.claude/Rules.md`、`.claude/SessionBrief.md`、`.claude/contexts/OLP-archive.md`
- 方法：先独立通读文档与全部源码并形成结论，再用探针程序逐条复现；结论定稿后才对照 PR #1 旧审计（`Docs/AUDIT_Part1_Plan_and_Framework.md`，分支 `cursor/audit-part1-framework-3999`），分歧见第八节。
- 环境：Linux，g++ 13.3，CMake 3.28。本环境无 Qt6、OpenCASCADE、Eigen、FCL，CMake 输出 `Qt6 not found ... building headless/stub UI layer` 与 `OpenCASCADE not found ... using built-in STEP/STL B-Rep kernel adapter`。MSVC 构建未验证。
- 约束：只读审计。未修改任何业务源码、Plan 或指导文档。探针程序在仓库外编译运行，源码与输出放在本次 artifacts（`audit_probes/`），不提交进仓库。

---

## 一、执行摘要

**Plan 对齐度（一句话）：Plan 文本对 v1.1 的模块、算法、熔断规则和 P0/P1/P2 边界基本忠实转写，但在 3.4 拍板依据、两条验收口径和阶段 0→1 的执行顺序上有实质偏离，综合判为「中高」，不是「高」。**

**框架结论：不合格。** 能编译、`olp_tests` 20/20 全绿，但绿灯测的是代码与自己的一致性，不是与 FANUC、URDF、真实 STEP 的一致性。第一部分点名的两大依赖（Qt6、OpenCASCADE）在代码里实际为零调用；运动学、几何内核、单位换算、命令栈四条主干都有静默错误。

**问题数量：P0 8 条，P1 14 条，P2 10 条。**

**最关键的 5 件事：**

1. **运动学约定错误，且被自反测试掩盖（P0-1、P0-2）。** `RobotModel` 里写好的 `signCorrections`、`offsetsRad` 从未被 `OpwKinematics` 读取。同一组关节角下，代码 FK 与仓库自带 URDF 的法兰位置最大相差 **1508.9 mm**（1000 组随机样本）。只要把这两个字段接上，偏差降到 **0.000000 mm**。J2/J3 耦合方向也和 FK 约定相反：只动 J2 10°，前臂转了 **20°**；按 FANUC 联动语义应为 0°。
2. **`OcctCadKernel` 没有调用任何 OpenCASCADE API（P0-3、P0-4）。** 它是逐行正则解析 STEP，网格是包围盒。4 个真实 FreeCAD STEP 全部返回「导入成功」，但拓扑大量丢失：`SteelHanger` 有 65 个面，只读到 8 个。曲面法向恒为 `(0,0,1)`。机器人 STL 是米制，却原样放进毫米场景。STEP 单位换算还取决于单位实体在文件中的行序：同一个米制文件，单位行在文件头时坐标是 200000，在文件尾时是 200，静默相差 1000 倍。
3. **命令栈铁律被绕过（P0-5）。** 场景导入、机器人替换、UFRAME 定义、路径生成都直接改写 `ProjectDocument`。仿真校验也会写回源数据：用户锁定的配置 `'N U B'` 被改成 `'F U T'`，`lockConfig` 仍为 true。仓库里没有 `SelectionManager` 和 `EventBus`。
4. **「Qt6 + OCCT 五层骨架」的表现层是空壳，并且被静默降级（P0-6）。** 全仓库零 Qt 代码（唯一命中是注释）。`OcctViewport::fitAll()` 是空函数体。`TcpGizmo` 没有 `AIS_Manipulator`，也没有工作线程。所有 `find_package` 都是 `QUIET`，找不到依赖时直接降级为桩，「能编译」不证明任何依赖被用上。
5. **验收门禁失效（P0-7、P0-8）。** 20 条断言里有 4 组是同义反复或自反闭环。README 和记忆文件写着「与真机 100% 对齐」、「Three.js」、「20/20 对拍」。`Compile.bat` 测试失败仍 `exit /b 0`。`main` 导出失败仍打印「Exported」并返回 0。`main` 自己的演示程序被它自己的碰撞模型判出 2 处碰撞（`Table_Clamp_Block_1`），照样导出 `.ls`。阶段 0 的 ROBOGUIDE 对拍、`.ls` 金样本、OCCT 拾取验证都没做，下游功能却已铺开。3.4 节的拍板依据是项目自己生成的合成 STEP。

**是否建议进入下一部分（指导文档阶段 2：焊缝、路径编辑、报告与 HTML）：不建议。** 阶段 2 会直接消费现在的几何内核、运动学和命令栈。不先修第七节的必修项就继续，错误会被固化进焊缝、路径和 `.ls`。

---

## 二、「第一部分」的定义与完成线对照

| 口径 | 出处 | 范围 |
| :--- | :--- | :--- |
| 接手摘要 | `SessionBrief.md` §1.4、§3.1 | CMake + C++20 + Qt6 + OpenCASCADE 五层骨架（`olp_core/cad/device/algo/io/app/ui` + `olp_tests`），完成全部源码、MSVC 2022 构建与单测验证 |
| Plan 活跃事项 | `Plan.md` §一.2（IN PROGRESS） | 在骨架上落地 M01（文档 + 命令栈 + **选择集 + 事件总线**）、M02（`ICadKernel` + **OpenCASCADE 实现**）、M03（三维视图 + 不挂接对象的 `TcpGizmo`）、M05/M06（M-10iA + OPW + J2/J3 耦合 + WPR）、M12（`.ls` 生成器） |
| 指导文档阶段 1 完成线 | 指导文档 §9.2 阶段 1、里程碑 M1 | 上表全部，再加：视口拾取、TcpGizmo 拖动预览与参考系、STEP/STL 与单位统一、场景树/挂枪/UTOOL/UFRAME、方案 A 替换、FK/IK/配置字/点动、M14 基础版工程文件、最简 `.ls` 在 ROBOGUIDE 中跑出同样轨迹 |

本报告把「第一部分」定为 **SessionBrief 骨架 + Plan §一.2 点名模块**，凡涉及这些模块的地方，按指导文档阶段 1 完成线判定。仓库里提前出现的阶段 2~4 代码（焊缝、路径、碰撞、仿真、HTML）按「提前占位」审查：写错了照样记问题，但不因其存在而给第一部分加分。

| 第一部分条目 | 实现现状（证据） | 判定 |
| :--- | :--- | :--- |
| CMake 五层 8 个目标 | 目标齐全（`CMakeLists.txt`），依赖全 `QUIET`，缺失时降级为桩 | 部分 |
| Qt6 | 全仓库无任何 Qt 类型或 `#include <Q...>`；`OLP_HAS_QT6` 宏从未被引用 | 未实现 |
| OpenCASCADE | `OcctCadKernel.cpp`、`OcctViewport.cpp` 只在 `#ifdef OLP_HAS_OCCT` 下 include 头文件，没有任何调用 | 未实现 |
| M01 文档 / 命令栈 | `ProjectDocument`（公开可写结构体）、`CommandManager`、`ModifyTargetPoseCommand`、`MacroCommand`、`ReverseOperationPathCommand` 存在 | 部分（铁律被绕过，见 P0-5） |
| M01 选择集 / 事件总线 / 后台任务 | 不存在（全仓库无 `SelectionManager`、`EventBus`、`TaskRunner`、`std::thread`、`std::atomic`） | 未实现 |
| M02 `ICadKernel` + OCCT 实现 | 接口在；实现是逐行正则解析器，网格为包围盒 | 伪实现 |
| M03 视图 + `TcpGizmo` | 视口为空函数；Gizmo 只有纯数学的吸附、预览、提交，参考系有错 | 伪实现 / 部分 |
| M05 机器人模型 | 参数硬编码在 `createFanucM10iA`；JSON 无人读取且与 C++ 矛盾；`loadFromUrdf` 不解析几何 | 部分 |
| M06 FK/IK/配置字/J2-J3/WPR | WPR 正确；FK/IK 约定错误；耦合方向错；配置字与圈数未核；点动面板不存在 | 不合格 |
| M12 `.ls` | 段结构正确；CONFIG 来源错误；无金样本；`appendReturnHome` 未接线 | 部分 |
| M14 工程文件（阶段 1 第 8 周） | 不存在 | 未实现 |
| GoogleTest / CI（S0-2） | 手写 `check()`；无 `.github/`；`vcpkg.json` 列了 gtest 但 CMake 未使用 | 未实现 |

---

## 三、Plan 对齐矩阵（Plan 文本 vs 指导文档 v1.1）

判定只评价 **Plan 文本相对指导文档**，四档为：严格一致 / 部分偏离 / 缺失 / 越权。代码是否做到，写在最后一列，不计入 Plan 判定。

两份 Plan 实质一致：`diff` 只有两处，一处是 `.claude/Plan.md` 第五节标题里的繁体字「與」，另一处是阶段 3 第 17 周措辞不同（「段内碰撞检测」与「采样检测」），范围、工期无差别。下文引用根目录 `Plan.md`。

### 3.1 范围与 3.4 方案

| # | 条目 | 指导文档 | Plan | 判定 | 实现现状 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | P0 范围 | §3.1 共 13 类 | §3.1 共 13 条，逐项对应并细化 | 严格一致 | 见第二节 |
| 2 | 首款机型选择依据 | §3.1：「具体按你们手头真机/ROBOGUIDE 能验证的型号定」；§12.1 列为立项前待确认 | 定为 M-10iA / ARC Mate 100iC，依据是 `SampleScenes` 里有现成的 URDF | 部分偏离 | 选型依据从「能验证」变成了「有现成资产」；§12.1 未关闭，ROBOGUIDE 可用性无记录 |
| 3 | P1 清单 | §3.2 + 各模块表中的 P1 项 | §3.2 覆盖主要 P1 项；IGES、ShapeFix、测量/剖切、CNT 近似、关节曲线、多语言、拆分/合并等模块级 P1 未汇总 | 部分偏离（轻微，未把 P1 提进 P0） | — |
| 4 | P2 边界 | §3.3 | §3.3 逐条相同 | 严格一致 | — |
| 5 | 3.4 方案 A 作为 P0 默认 | §3.4 | 方案 A 为 P0，B/C 留 P1 | 严格一致 | `WorkcellLayoutService` 有原型，见 P1-9 |
| 6 | 3.4 拍板依据 | §3.4、§9.2 阶段 0、§12.6、`Docs/SM.txt`：要拿到客户 **2~3 份真实**带机器人场景，并说明来源软件，再决定方案 | 依据是 `Full_Welding_Cell_With_Robot.step`。该文件头为 `FILE_NAME(...,('MimoVR'),'StepBrepBuilder','Si_FastSuite_OLP',...)`，是项目自生成的 12 个长方体，没有装配、颜色、变换实体 | **部分偏离** | 用自己造的数据证明自己的方案，验证是循环的；§12.6 仍未关闭 |
| 7 | 方案 A 场景事实 | — | §2.2 写「12 个实体」，但列出了 2+5+1+3+2 = 13 个名字，其中「安装法兰过渡台」在文件中不存在；§2.3 第 1 步又写「解析出 11 个实体」；`OLP-archive.md` 也写 11 | 部分偏离（事实错误） | 实测文件内 12 个 `MANIFOLD_SOLID_BREP` |
| 8 | 方案 A 初始关节角 | §3.4、M04：初始关节角「按外形反算」或手工输入 | §2.3、M04 都没有这一步 | 缺失 | 代码直接放在 Home，与静态外形相差约 1 m（P1-9） |

### 3.2 架构与铁律

| # | 条目 | 指导文档 | Plan | 判定 | 实现现状 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 9 | 五层与 CMake 映射 | §4.1 L1~L5 | §4.1 七个库，依赖方向一致 | 严格一致 | 目标齐全；`olp_tests` 链接 `olp_ui`（P2-7） |
| 10 | 铁律 1：算法层无 Qt | §4.1「运动学、碰撞、插补、后处理做成纯 C++ 库」 | §4.2 把 `olp_io` 整体列为纯 C++；但 §4.1 表和阶段 2 又把基于 `QPdfWriter` 的 M13 2D PDF 放进 `olp_io`（`Plan.md` 第 113、312 行） | **部分偏离（Plan 自相矛盾）** | 目前无 Qt 代码，矛盾尚未爆发；阶段 2 做 2D PDF 时必然触发 |
| 11 | 铁律 2：所有修改走命令 | §4.1、§2.3.1 | §4.2 第 2 条，并强调第 1 天落地 | 严格一致 | 被绕过（P0-5） |
| 12 | 铁律 3/4：派生可重算、位姿相对 UFRAME、`userModified`、拓扑签名 | §4.3 | §4.2、§4.3 同义，签名补了 SHA-256 | 严格一致 | 位姿相对 UFRAME 已做；签名无哈希；缓存失效后无人调度重算 |
| 13 | M07/M08 所在层 | §4.1 应用层 | `src/app/seam/`、`src/app/toolpath/` | 严格一致 | 实现放在 `olp_algo`（P2-7） |

### 3.3 模块 M01~M15

| # | 模块 | 判定（Plan 文本） | 说明 | 实现现状（第一部分视角） |
| :--- | :--- | :--- | :--- | :--- |
| 14 | M01 | 严格一致 | 文档、命令、合并、200 步、选择集、事件总线、后台任务与取消令牌均写入；设置、日志、minidump 分散在 §3.1.13 与 §4.1 | 选择集、事件总线、后台任务、设置、日志均不存在 |
| 15 | M02 | 严格一致 | 接口清单与 OCCT API 点名比指导文档更具体；跨零件 `< 1.0 mm` | 伪实现（P0-3）；接口默认公差为 2.0 mm |
| 16 | M03 | 严格一致 | TcpGizmo 五条逐字对应；补了验收 5 的 30 FPS / 100 ms | 视口空壳；Gizmo 参考系错误（P1-10） |
| 17 | M04 | 部分偏离 | 缺「初始关节角反算/手输」；「机器人基座与 FANUC 世界坐标换算」只在 UFRAME 一句带过，指导文档 M04 单列为 P0 | 平移对齐、无朝向、非命令（P1-9、P0-5） |
| 18 | M05 | 严格一致 | JSON 规范、Home、耦合开关 | JSON 无人读取且与 C++ 矛盾（P1-13） |
| 19 | M06 | 部分偏离 | J2/J3 默认公式在对拍前写死（指导文档要求「需在 ROBOGUIDE 中核对符号和系数」）；圈数对应哪几根轴没定义（指导文档：「需按控制器手册核对」） | 约定错误（P0-1、P0-2、P1-8） |
| 20 | M07 / §6.1 | 严格一致 | 公式一致，并补 `‖n1+n2‖ < 1e-5` 报错 | 纯函数正确；采样与默认法向有错（P1-1、P1-13） |
| 21 | M08 / M09 | 严格一致 | 默认事件表与指导文档示例 `.ls` 一致 | 提前实现，有静默错误（P1-2、P1-3、P1-4） |
| 22 | M10 / §6.5 | 严格一致 | 豁免半径 8~15 mm 是细化 | 玩具模型（P1-5、P1-6） |
| 23 | M11 | 严格一致 | 节拍标注为估算 | 提前实现，语义有错（P1-7） |
| 24 | M12 | 部分偏离 | 遗漏指导文档 M12 第一行的 P0 项「与品牌无关的中间表示」 | 直接从 `Program` 拼文本，无 IR（P1-11） |
| 25 | M13 | 严格一致 | 2D PDF + HTML 为 P0，3D PDF 熔断 | HTML 是 2D canvas 伪实现（P1-12） |
| 26 | M14 | 严格一致 | — | 不存在 |
| 27 | M15 | 严格一致 | GoogleTest、回归库、CI、安装包 | 均不存在 |

### 3.4 关键算法、3D PDF 熔断、阶段与验收

| # | 条目 | 判定 | 说明 |
| :--- | :--- | :--- | :--- |
| 28 | §6.1 焊缝坐标系 | 严格一致 | `X=t, Z0=-b, Y=Z0×X, Z=X×Y`，`R_tcp = R_seam·Rx·Ry·Rz` |
| 29 | §6.2 逆解与选解 | 严格一致 | Plan 写明 J1/J4/J6 圈数展开，焊接段禁止换配置 |
| 30 | §6.3 L 插补配置一致 | 严格一致 | Plan 补了 15° 阈值（代码用的是 25° 加权范数，见 P2-6） |
| 31 | §6.4 WPR 与万向锁 | 严格一致 | Plan 写定 `w=0, r=atan2(-r12, r22)`。本审计代数核对：`p=+90°` 时 `r12=sin(w−r)`、`r22=cos(w−r)`；`p=−90°` 时 `r12=−sin(r+w)`、`r22=cos(r+w)`；两种情况下 `w=0` 都给出 `r=atan2(−r12, r22)`，公式正确 |
| 32 | §6.5 自转角扫描 | 严格一致 | Plan 写「72 组」，闭区间实为 73 组（代码就是 73），不影响 |
| 33 | J2/J3 耦合 | 部分偏离 | 见 #19 |
| 34 | 3D PDF 五天熔断 + HTML 保底 | 严格一致 | S0-6、M13、R03/R04 四条规则完整 |
| 35 | 阶段 0（S0-1~S0-8） | **部分偏离（执行顺序）** | 文本覆盖了指导文档阶段 0 的全部预研项。但 Plan §一 的实际执行，是在 S0-3（OCCT 拾取）、S0-4（ROBOGUIDE 20 组对拍）、S0-5（`.ls` 金样本）都未执行（标为 NEXT）的情况下，先实现了 M06、M12、M03，以及阶段 2~4 的 M07/M08/M10/M11/M13。这与指导文档 §0.5「先用 2~3 周做技术预研……然后打通端到端」相反。运动学约定错误就是这样被固化的。S0-1「已完成」的依据是合成文件（#6） |
| 36 | 阶段 1 / M1 | 严格一致（文本） | 实现离 M1 很远（第二节） |
| 37 | 阶段 2~5 / M2 / M3 | 严格一致 | — |
| 38 | 团队与工期弹性 | 部分偏离 | §9.3 工期弹性（2 人约 10~12 个月；方案 B/C 加工期）未转写 |
| 39 | 风险清单 | 部分偏离 | R01~R09 缺独立条目：碰撞检测性能、CNT/节拍与真机差异、通用范围蔓延 |

### 3.5 九项验收指标

| # | 指导文档 §11 | Plan §八 | 判定 |
| :--- | :--- | :--- | :--- |
| A1 | 运动学：ROBOGUIDE 随机 100 组，FK `<0.01 mm / <0.001°`，IK 同配置还原 | 相同 | 严格一致 |
| A2 | `.ls`：全部可被 ROBOGUIDE 加载或 MakeTP 编译，运行一致 | 相同 | 严格一致 |
| A3 | 焊缝姿态：**T 型角焊、搭接、箱体内角**三类，与**焊接顾问认可**的姿态偏差 `<5°` | 改为 **T 型、管法兰、箱体内角**，基准改为「标准工艺姿态」 | **越权**：无修订记录地改了样本类型和判定基准；搭接焊从验收中消失 |
| A4 | 10 个构造碰撞场景全检出，标准路径零误报 | 相同，并点名夹具 | 严格一致 |
| A5 | 100 万三角面下 ≥30 FPS、<100 ms、一次拖动一条撤销、Esc 可取消 | 相同 | 严格一致 |
| A6 | 2D PDF / HTML / 条件 3D PDF | 相同 | 严格一致 |
| A7 | **至少 2 份带机器人外形的真实整场景 STEP** 导入替换，基座偏差 `<1 mm` | 改为「`Full_Welding_Cell_With_Robot.step` 等」，加内控 `<0.01 mm` | **越权（弱化）**：数量与「真实」两项条件都丢了，而点名的文件恰好是合成件 |
| A8 | 2 小时稳定、100 步撤销、保存重开一致 | 相同，并增加 `userModified` 一致 | 严格一致 |
| A9 | 真机 2 件空走、1 件实焊 | 相同 | 严格一致 |

### 3.6 记忆与文档治理

| # | 条目 | 判定 | 证据 |
| :--- | :--- | :--- | :--- |
| 40 | `Rules.md`：`Plan.md` 只留 1~3 个活跃项 | 偏离 Rules | 两份 Plan 都是 386 行的全量计划 |
| 41 | `Rules.md` 主线描述 | 过期 | 第 17 行仍写「当前主线（基于 3D 视觉的免示教……）」，与 `CLAUDE.md`、`SessionBrief` 冲突 |
| 42 | `OLP-archive.md` §0.2 对代码的描述 | **与代码不符** | 列出了 `SelectionManager`、`EventBus`、`RobotReplacementService`、M14 序列化器、URDF 解析器、OCCT `STEPCAFControl_Reader`/`TopExp`/`BRepAdaptor_Curve` 支持、Qt6 主窗口与停靠面板，这些在仓库中都不存在（P1-14） |

---

## 四、问题清单

严重度定义：
- **P0**：进入下一部分前必须修。否则核心结果（几何、运动学、`.ls`）静默错误，或者硬约束失效，或者门禁失去意义。
- **P1**：已实现部分写错，或与契约不符，下一阶段一定会踩到。
- **P2**：边界债、潜在风险、文档不一致。

### 4.1 P0（8 条）

#### P0-1 OPW 运动学约定错误：符号修正与零位偏置未接线

- **位置**：`src/algo/OpwKinematics.cpp` 中 `forwardKinematics`（第 29~90 行）、`inverseKinematics`（第 92~204 行）、`computeConfig`、`checkJointLimits`；`src/device/RobotModel.cpp` 第 24~25 行；`resources/robots/fanuc_m10ia.json` 第 17 行。
- **现象**：`createFanucM10iA` 设置了 `offsetsRad = {0,0,−π/2,0,0,π}` 和 `signCorrections = {1,1,−1,−1,−1,−1}`（ROS-Industrial M-10iA 的 OPW 约定），但 FK/IK 从未读取这两个字段，直接把输入关节角当作标准 OPW 角使用。
- **证据**（探针 `probe_kin`）：
  - 全零关节时，代码 FK 法兰在 `(0, 0, 1790) mm`，工具轴朝上；仓库自带 `fanuc_m10ia_standalone.urdf` 的法兰在 `(890, 0, 1200) mm`，工具轴朝 +X。
  - 1000 组随机关节角下，代码 FK 与按 URDF 逐关节复合的 FK，法兰位置最大相差 **1508.928 mm**。
  - 先按 `q = sign·J − offset` 换算再调用同一个 FK，偏差变为 **0.000000 mm**，工具轴偏差 0.000001°。这说明 FK/IK 的数学本身没错，错在约定没接上。
  - 同一物理姿态（URDF Home：J5 = −90°，前臂水平、焊枪朝下），代码 IK 给出的关节角是 `[0, 0, 90, 0, 90, 0]`：J3 差 90°，J5 符号相反。限位检查因此用 FANUC/URDF 的限位去判断一个零点和方向都不同的关节角。例如物理上合法的 J3 = 170° 在代码里是 260°，会被判超限；物理 J3 = −170° 会被误判为在限位内。
- **为何违规**：指导文档 §6.2、M06、验收 A1 要求关节角与同型号 ROBOGUIDE 一致。现状下，代码输出的所有关节量（限位判定、配置字 F/N 与 U/D、Home、点动读数、方案 A 的初始姿态、`BakedFrame` 连杆矩阵）都与 FANUC 和自家 URDF 不一致。UTOOL 也挂在一个与 FANUC 法兰坐标系相差固定旋转的法兰系上，用户从示教器抄来的 UTOOL 数值会被套在错的坐标系里。
- **建议**：明确一条唯一的换算链「示教器关节角 → 耦合 → URDF/ROS 关节角 → `q = s·J − o` → 标准 OPW」，FK 和 IK 对称实现。法兰系与 URDF 的 `flange`/`tool0` 对齐。新增两类测试：以 URDF 链为金样本的 FK 对比（零位 + 1000 组随机，`<0.01 mm`）；把方案 A 静态外形当作 Home 姿态的叠合检查。删除或接上 JSON，保证只有一份参数真值。

#### P0-2 J2/J3 耦合方向与 FK 约定相反，且被宣称「与真机 100% 对齐」

- **位置**：`src/algo/OpwKinematics.cpp` 第 12~27 行（`modelToDisplayJoints` / `displayToModelJoints`）；`RobotModel::j2J3CouplingCoeff = 1.0`；`README.md` 第 30 行。
- **现象**：代码直接在标准 OPW 空间里做 `q3 = J3 + 1.0·J2`。标准 OPW 的 q2、q3 都绕 +Y，于是前臂的绝对角变成 `q2 + q3 = 2·J2 + J3`。
- **证据**（`probe_kin` K3）：保持示教器 J3 = 0，J2 从 0 改到 +10°，代码前臂轴转了 **20.000°**。按 URDF 约定加 ROS-I 式耦合 `j3_urdf = J3 + J2`，前臂转 **0.000°**，这才符合 FANUC「J3 相对水平面」的联动语义。
- **为何违规**：结论不依赖 ROBOGUIDE 的最终系数。如果 FANUC 存在联动，前臂绝对角应与 J2 无关，代码给出 2 倍；如果不存在联动，就不应开启耦合。两种假设下当前实现都是错的。指导文档 M06 要求先在 ROBOGUIDE 核对符号和系数，README 却已宣称「100% 对齐」。
- **建议**：耦合只在「示教器角 ↔ URDF 角」这一层做，并经过 P0-1 的符号/偏置链。在 S0-4 用 ROBOGUIDE 采样确认系数正负。确认前，README 和记忆文件改为「未核定」。

#### P0-3 `OcctCadKernel` 是伪实现：不调用 OCCT，逐行正则解析，真实 STEP「成功」但丢失大部分拓扑

- **位置**：`src/cad/OcctCadKernel.cpp`：第 11~30 行只有 include；`importStep` 第 63~384 行；非平面面统一标为 `Cylinder` 在第 277~279 行；包围盒兜底在第 341~351 行；网格为包围盒在第 376~377 行；`evaluateEdgeAtArcLength`（第 511~527 行）按直线插值；`evaluateFaceOutwardNormal`（第 529~539 行）忽略采样点；`tessellate`（第 541~552 行）返回包围盒；`computeEdgeSignature` 不计算哈希。
- **现象**：
  - 找到 OCCT 时，代码路径也完全相同，没有任何 `STEPCAFControl_Reader`、`TopExp`、`BRepAdaptor_Curve`、`BRepMesh_IncrementalMesh` 调用。
  - 用 `std::getline` 加正则逐行匹配实体，跨行实体会被截断。
  - 只认 `PLANE` 和 `MANIFOLD_SOLID_BREP`；所有曲面一律标为圆柱，法向保持默认 `(0,0,1)`；所有边都当直线处理。
  - 忽略 `ORIENTED_EDGE` 和 `EDGE_CURVE` 的方向标志（指导文档要求处理 `TopAbs_REVERSED`），只取第一个 bound（内孔丢失）。
  - 不处理装配、颜色、变换（`NEXT_ASSEMBLY_USAGE_OCCURRENCE`、`STYLED_ITEM`、`ITEM_DEFINED_TRANSFORMATION`）。
  - 渲染网格与碰撞网格都是 12 个三角形的轴对齐包围盒。
- **证据**（`probe_more` M6，四个真实 FreeCAD/OCCT 6.8 导出件，全部返回 `ok=1`）：

  | 文件 | 实际 `ADVANCED_FACE` / `EDGE_CURVE` | 读到的面 / 边 | 曲面法向 |
  | :--- | :--- | :--- | :--- |
  | `Workpiece_PipeFlange_DN15.step` | 19 / 37 | 10 / 18，包围盒 Z 向厚度为 0 | 7 个曲面全是 `(0,0,1)` |
  | `Workpiece_U_Bracket_L70.step` | 24 / 63 | 9 / 24 | — |
  | `Workpiece_SquareTube_80x80x5.step` | 18 / 48 | 10 / 31 | 5 个曲面全是 `(0,0,1)` |
  | `Workpiece_SteelHanger_SAET200.step` | 65 / 177 | **8 / 30** | 1 个曲面 `(0,0,1)` |

  两个测试用的合成件（`Workpiece_TJoint_With_Clamps.step`、`Full_Welding_Cell_With_Robot.step`）恰好是一行一个实体、只有 `PLANE` 和 `LINE` 的长方体，所以测试全绿。
- **为何违规**：指导文档 §7 与 M02 规定 CAD 内核为 OCCT XCAF，P0 能力包括装配、颜色、单位、弧长求值、曲面法向、粗细两套网格、签名。现在曲面法向「看起来合法」（单位向量而非零向量），不会触发 `computeSeamFrame` 的退化检查，管法兰焊缝的姿态会静默错误。
- **建议**：OCCT 可用时完整走 XCAF 加拓扑与网格 API。OCCT 不可用时 `importStep` 应返回失败并说明原因，不能返回「成功导入 B-Rep」。用 4 个真实 STEP 建回归：面数和边数与文件实体数一致，曲面法向与解析值一致。

#### P0-4 单位统一失效：STL 米制原样入库；STEP 单位换算依赖行序

- **位置**：`src/cad/OcctCadKernel.cpp` 第 101~107 行（逐行检测 `SI_UNIT`）、第 419 行（注释声称会做米→毫米换算，代码没有换算）。
- **证据**：
  - `probe_more` M8：`link_2.stl` 包围盒 Z 最大 0.669，单位是米，场景约定是毫米，机器人网格会小 1000 倍。Test 2 只断言「三角形非空」。
  - `probe_more` M7：把 T 型件单位改为米。单位行在文件头时，坐标 X 最大值为 200000（正确换算）；单位行移到文件尾时为 200（未换算），静默相差 1000 倍。4 个真实 FreeCAD 文件中有 3 个把单位实体放在文件尾（例如 `Workpiece_U_Bracket_L70.step` 第 2154 行）。
- **为何违规**：指导文档 §2.3 第 6 点和 M02「单位统一 P0」：「否则后期到处是 1000 倍和轴向错误」。
- **建议**：STEP 单位交给 XCAF 或两遍解析处理；STL 单位由机器人包加载器按 URDF 约定显式换算；测试断言 `base_link` 高度约 278 mm。

#### P0-5 命令栈铁律被绕过：服务层、主流程、仿真直接写文档

- **位置**：
  - `include/olp/app/CommandManager.h` 第 12~23 行：`ProjectDocument` 是全公开可写结构体。
  - `src/app/WorkcellLayoutService.cpp` 第 42、58、61、76~79、89~90 行：直接 `push_back`、改 `visible`/`collisionRole`/`isDirty`。
  - `src/algo/SeamAndToolpathAlgo.cpp` 第 280 行：直接覆盖 `inOutOperation.targets`。
  - `src/algo/CollisionAndSim.cpp` 第 202 行：`target.config = chosen->config`。
  - `src/main.cpp` 第 79、107~109 行。
- **证据**：
  - `probe_more` M3：用户在 APPROACH 上锁定 `'N U B, 0, 0, 0'`，跑一次 `validateAndSimulateProgram` 后，`target.config` 变成 `'F U T, 0, 0, 0'`，`lockConfig` 仍为 1。原因：`selectBestSolution` 在 `forbid=false` 时只加 100 罚分，然后仿真把选中的解写回源数据。
  - 路径重新生成不经过命令，目标 id 又会被复用（`op.id + "_T_WELD_" + i`）。撤销栈里旧的 `ModifyTargetPoseCommand` 之后会把旧位姿写到语义已变的新目标上。
  - 全仓库无 `SelectionManager`、`EventBus`、`TaskRunner`；缓存失效（`cache.valid = false`）后没有任何机制调度重算。
- **为何违规**：用户点名的硬约束「UI 改数据必须走 CommandManager」，以及指导文档 §2.3.1「后期补等于重写编辑器」。仿真改写源数据还会让「撤销/重做 100 步一致」（A8）在真实交互中失效。
- **建议**：增加 `ImportSceneCommand`、`ReplaceRobotCommand`、`DefineUFrameCommand`、`GenerateToolpathCommand`、`SetTargetConfigCommand` 等命令。仿真只写 `TargetCache`。UI 只拿 `const ProjectDocument&`。补最小可测的选择集与变更通知，并写一条测试证明 UI 层无法绕过命令写文档。

#### P0-6 「Qt6 + OCCT 五层骨架」的表现层是空壳，依赖被静默降级

- **位置**：`CMakeLists.txt` 第 23~51 行（全部 `find_package(... QUIET)`，缺失时只打一行 STATUS）；`src/ui/MainWindow.cpp`（10 行，不是 `QWidget`）；`src/ui/OcctViewport.cpp` 第 17~19 行（`fitAll()` 只有注释）；`include/olp/ui/TcpGizmo.h` 第 20~21 行（`ikReachable`、`hasCollision` 从不被赋值）。
- **证据**：
  - 全仓库对 `#include <Q`、`QWidget`、`QApplication`、`Q_OBJECT` 的检索只命中 `OcctViewport.h` 第 21 行的注释。
  - `OLP_HAS_QT6`、`OLP_HAS_FCL`、`OLP_HAS_EIGEN3` 三个宏由 CMake 定义，但没有任何源码引用。
  - `vcpkg.json` 列出 eigen3、nlohmann-json、opencascade、qtbase、fcl、gtest；CMake 未接 vcpkg toolchain，源码也不使用这些库。
- **为何违规**：SessionBrief 对第一部分的定义就是「CMake + Qt6 + OpenCASCADE 五层骨架」；Plan §一.2 点名 M03 三维视图与 TcpGizmo（复用 `AIS_Manipulator`、工作线程只算最新帧）。现在「能编译」在任何机器上都成立，不证明任何依赖被用上。
- **建议**：产品配置下 Qt6、OCCT 用 `REQUIRED`。保留一个显式的 `OLP_HEADLESS` 选项给纯算法 CI。表现层先做出最小真窗口：`QMainWindow` + `V3d_View` + `AIS_InteractiveContext` + 边/面拾取 + `AIS_Manipulator` 取增量。

#### P0-7 验收门禁失效：测试自洽假阳性，README/记忆虚报，脚本与主程序吞掉失败

- **位置**：`tests/test_main.cpp`；`README.md` §1.3、§4；`.claude/contexts/OLP-archive.md` §0.2；`Tools/Compile.bat` 第 35~41、58~62、76 行；`src/main.cpp` 第 117~137 行。
- **证据**：
  - 测试命题与声称不符（详见第五节）：Test 2 断言的 `c2=600` 是硬编码常量，不是从 URDF 解析的；Test 3 是同一错误模型的 FK→IK→FK 自反闭环，README 却称「100 组对拍」；Test 4 用同一个合成长方体的包围盒当真值；Test 7 断言的 `'N U T, 0, 0, 0'` 是结构体默认值，没有跑 IK。
  - `Compile.bat` 测试失败只打印 WARNING，最后 `exit /b 0`。第 37~38 行在括号块内使用 `%ERRORLEVEL%`，批处理会在解析整个块时展开它，CMake 配置失败检测不到。脚本虽然开了 `enabledelayedexpansion`，却没用 `!ERRORLEVEL!`。
  - `main` 不检查 `exportToFile` 和 `exportSingleFileHtml` 的返回值。本次实测在 `build/` 不存在时，仍打印「Exported FANUC .ls: /workspace/build/WELD_001.ls」并返回 0，文件并不存在。
  - `main` 自己的演示程序被自己的碰撞模型判出 2 处碰撞（`probe_more` M1：APPROACH 间隙 −8.3 mm、WELD_START 间隙 −10.8 mm，都撞 `Table_Clamp_Block_1`）。`main` 不读 `issues`，照样导出 `.ls` 并宣告成功。
- **为何违规**：指导文档 §11 的验收依赖外部金样本（ROBOGUIDE、真机、浏览器/Acrobat）。用同名的内部闭环填满测试，会让阶段门禁形同虚设。这正是本次审计要找的「测试全绿不等于正确」。
- **建议**：测试按真实命题改名；9 项验收单列，标为「未执行」；`Compile.bat` 失败返回非零并改用 `!ERRORLEVEL!`；`main` 遇到导出失败或校验问题时返回非零；README、`OLP-archive.md`、SessionBrief 改为如实描述。

#### P0-8 阶段 0 外部门禁未执行，下游已铺开；3.4 拍板依据是自生成合成件

- **位置**：`Plan.md` §一（S0 对拍标为 NEXT，同时把 M06、M12 列为进行中）；`Plan.md` §六 S0-1 标「已完成」；`SampleScenes/Scene2_Full_Workcell_STEP/Full_Welding_Cell_With_Robot.step` 第 4 行文件头；仓库中没有任何 ROBOGUIDE 采样、`.ls` 金样本或 3D PDF 熔断记录。
- **为何违规**：指导文档 §0.5、§9.2 阶段 0 的设计意图，是在铺开之前先验证三个最高风险：`.ls` 能否加载、逆解是否与 ROBOGUIDE 一致、3D PDF 可行性。现在的顺序倒过来了，P0-1、P0-2 就是代价。方案 A 的拍板只证明「我们生成的文件符合我们的方案」，没有回答指导文档 §12.6「客户的场景文件长什么样」。
- **建议**：阶段 2 开工前，先关闭 S0-3（真实 STEP + OCCT 拾取）、S0-4（ROBOGUIDE 20 组）、S0-5（金样本 `.ls`），并记录 S0-6 的熔断决策。把 3.4「已拍板」降级为「暂定」，拿到客户真实场景后复核，复核前不得把 A7 视为可验收。

### 4.2 P1（14 条）

| ID | 位置 / 符号 | 现象与证据 | 为何违规 | 建议 |
| :--- | :--- | :--- | :--- | :--- |
| P1-1 | `SeamAndToolpathAlgo::buildSeamFromPickedEdge` 采样循环（第 124~149 行） | 循环条件是 `s <= sEnd`，`std::min(s, sEnd)` 永远不会把最后一个点夹到 `sEnd`，修剪后的终点经常采不到。`probe_app` S1（400 mm 边）：默认步长 20 漏 **10 mm**，步长 50（测试和 `main` 用的值）漏 **40 mm**，只有步长 30 恰好整除时才不漏；`main` 的 370 mm 焊缝漏 10 mm | 焊缝末端漏焊，`.ls` 静默少焊一段（M08「起止点内缩/外延」语义被破坏） | 循环结束后强制补一个 `sEnd` 采样点；测试断言首尾弧长等于修剪后的端点 |
| P1-2 | `generateOperationTargets` 第 229~250 行的 `isFirst`/`isLast` 分支链 | 只有 1 个采样帧时，该点既是首点也是尾点，只走 `isFirst` 分支，只挂 `Arc Start`。`probe_short`：20 mm 短焊缝生成的 `/MN` 只有 `Arc Start[1]`，**没有 `Arc End`**，RETRACT 在起弧状态下运行。此条 PR #1 也发现了，本审计已复现 | M09 默认规则要求终点带 `Arc End`；电弧永不熄灭是工艺事故 | 单帧时同时生成起点和终点（至少两点），或拒绝生成并报错 |
| P1-3 | `generateOperationTargets` 第 162~173、252~260 行 | `savedMods` 收集所有 `userModified` 目标（包括 APPROACH/RETRACT，它们的 `arcLengthRatio` 默认为 0），只按比例 ±0.05 匹配，匹配后不消耗。`probe_app` S9：用户把 APPROACH 下移 30 mm，重新生成后这个偏移被套到 **WELD_START** 上，APPROACH 自己的修改丢失 | 违反 §4.3「按弧长匹配保留手工修改」：保错了点，且一条修改可以复制到多个点 | 只匹配焊接角色；一对一消耗；对 APPROACH/RETRACT 单独按角色保存 |
| P1-4 | `ReverseOperationPathCommand::toggleReverse`（`CommandManager.cpp` 第 132~166 行） | 只交换角色、名字和事件类型，不交换 `motion`、`speedValue`、`useWeldSpeed`、`termination`。`probe_app` S6：反向后变成 `L P[2:WELD_START] WELD_SPEED FINE`（起弧前用焊接速度进枪）和 `L P[3:WELD_END] 200mm/sec FINE`（**整条焊缝以 200 mm/s 起弧焊接**）；APPROACH 变成 L 运动，RETRACT 变成 J 运动。焊枪姿态不随方向重算，推焊会变成拖焊；`seam.reversed` 不同步；Undo 不恢复自定义名字 | M08「路径反向」要求反向后程序仍正确；当前会产生焊接缺陷 | 反向时按角色重新套用 M09 默认参数（或交换整组运动属性）；按新前进方向重算姿态；同步焊缝方向；Undo 保存完整快照 |
| P1-5 | `CollisionAndSimEngine::checkTorchPoseCollision` 第 68~74 行；`core::Operation::targetWorkpieceId` | 豁免条件是 `node.id == activeWorkpieceId || role == Workpiece`，所有工件角色都被豁免。`probe_more` M2：焊丝尖端插入**非当前**工件 `Workpiece_BoxWall_Side` 1 mm，焊接角色下判「无碰撞」，非焊接角色下判碰撞（−3.0 mm）。另外数据模型只有单个 `targetWorkpieceId`，表达不了 T 型焊缝的两块母材；`UnitsSettings::wireTipExemptionRadiusMm` 没人读 | 指导文档 M10：只对当前焊缝豁免焊丝尖端，邻件和夹具要严格检测 | 豁免集合取焊缝两侧母材（`face1PartId`、`face2PartId`），只在尖端半径内生效；`Operation` 改为工件集合（与 PR #1 修法的分歧见第八节） |
| P1-6 | `checkTorchPoseCollision`、`scanTwistAngles`（第 93~134 行） | 碰撞体是沿焊枪轴的 4 个球，对象是实体包围盒；不检测机器人连杆，不用 FCL，段内采样只烘焙关节不做碰撞。因为球都在焊丝轴上，绕该轴的自转不改变任何球的位置，扫描的碰撞维度恒定。`probe_app` S4：73 个角度全部可达且无碰撞，最小间隙恒为 **13.0000 mm** | §6.5 扫描的价值就在于碰撞随自转角变化；R08 的豁免与指导文档 M10「连杆+焊枪 vs 工件/夹具」都没有落地 | 阶段 3 前不要以「已实现自转扫描」对外；接入连杆网格和非轴对称焊枪网格（FCL/Coal） |
| P1-7 | `validateAndSimulateProgram` 第 157~160、176~187、206~221、254~263 行 | `ArcStart` 在运动到 WELD_START **之前**就置位，进枪段被算作焊接时间（`probe_app`：到达 WELD_START 前有 4 帧 `arcActive=true`；焊接时间 35.225 s 里有 0.225 s 是进枪段）。这与 `.ls` 语义相反：后处理把 `Arc Start` 写在运动行之后。其他问题：超限被报成 Unreachable 或 ConfigJump（`JointLimitExceeded` 从未使用）；奇异、接近限位、接近碰撞不进 Issues；L 段无解时瞬移到终点且不记问题；J 段没有速度曲线，忽略 % 速度和关节限速 | M11 节拍分项与问题分类；§6.3 | 事件在到达目标点后生效；问题类型写全并全部进 Issues；速度曲线在阶段 3 按 M11 实现 |
| P1-8 | `OpwKinematics::inverseKinematics` 第 187~198 行，`computeConfig` 第 266~272 行，`checkSingularity` 第 294~304 行，`selectBestSolution` 第 222~231 行 | 圈数展开只做 J6（注释写 J4 和 J6，Plan 要求 J1/J4/J6）。配置字圈数取 J1/J4/J6，而 FANUC 默认通常是 J4/J5/J6（`$SCR_GRP[].$TURN_AXIS` 可配，**待核**）；`probe_kin` K5：J5 = 185° 时圈数不体现。J4 恰为 180° 时圈数记为 1。没有肩部奇异检测；「可操作度」是两个正弦相乘，不是雅可比指标；用户锁定的配置在 `forbid=false` 时只是软约束 | M06：腕/肘/肩三类奇异、圈数按手册核对、锁定配置优先级最高 | 圈数轴与 F/N、U/D、T/B 判据按 ROBOGUIDE 冻结；补肩部奇异与雅可比最小奇异值；锁定即硬约束 |
| P1-9 | `WorkcellLayoutService::importFullSceneAndReplaceRobot` 第 22~58 行 | 用名称启发式识别静态机器人（任何包含 `FANUC_` 的实体都会被隐藏并排除出碰撞），用包围盒中心加底面高度作为基座，只做平移不对齐朝向，初始关节直接取 Home。`probe_more` M5：替换后的机器人在 Home 下法兰位于 `(−100, 0, 2190)`，被替换的静态焊枪包围盒在 x 790~920、z 1380~1680，**相差约 1 m**；Test 4 只检查基座平移。另外，FANUC 世界坐标原点通常位于 J1 轴与 J2 轴所在水平面的交点（距安装面 c1 = 450 mm），代码以安装面为基座原点，UFRAME 数值与控制器交换时可能差 450 mm（**待核**） | §3.4 方案 A：拾取安装面、对齐、初始关节按外形反算或手输；M04「基座坐标与 FANUC 世界坐标换算 P0」 | 以 `Robot_Pedestal_Base_Top` 平面的原点和法向加用户指定朝向构造基座；提供反算或手输初始关节；在 ROBOGUIDE 核对世界坐标原点；整个流程走命令 |
| P1-10 | `TcpGizmo::updateDragDelta` 第 55~64 行，`commitDrag` 第 75~99 行 | World 与 UFrame 走同一分支（Gizmo 拿不到 UFRAME→世界的变换，`probe_app` S8d：两者结果相差 0.000 mm）；Seam 与 Tool 走同一分支（位姿叠加了焊接角，焊缝系不等于工具系）；预览不算 IK、不做碰撞，`ikReachable`/`hasCollision` 从不赋值；无 `AIS_Manipulator`、无工作线程。`probe_app` S8a：原地点击不拖动也会产生 1 条撤销记录，并把 `userModified` 置为 true | M03 第 2、3 条；A5「一次拖拽一条记录」的本意是一次**有效**拖拽 | 为 Gizmo 注入 UFRAME/世界变换与焊缝系；增量为零时不提交；预览回调接 IK 与碰撞，放在工作线程，只保留最新一帧 |
| P1-11 | `FanucLsPostProcessor::generateLsString` 第 72、82~84、120 行 | CONFIG 直接取 `Target::config`：未跑仿真时是默认 `'N U T, 0, 0, 0'`，跑了仿真则来自 P0-1 的错误运动学（`main` 流程四个点都是 `'F U T'`）；`appendReturnHome` 从不读取；`MotionType::C` 被写成 `L`；速度按整数截断；没有品牌无关中间表示；没有金样本比对 | M12：配置字必须来自求解；最简 `.ls` 要能在 ROBOGUIDE 加载运行 | 缓存无效时拒绝导出；接上回 Home；C 运动未实现时直接报错；建 IR；按 S0-5 金样本逐行比对 |
| P1-12 | `HtmlAnimationExporter::exportSingleFileHtml` 第 48~110 行 | 输出的是 2D canvas，只画 TCP 折线；写入的 `sceneParts` 在 JS 里没有被使用；没有 Three.js、GLB、机器人、旋转缩放和倍速。README 写的是「离线 3D 动画包」 | M13 P0 保底交付物要求 Three.js + Base64 GLB，可旋转、缩放、倍速、拖动进度；S0-7 也没有做 | 降级描述为「TCP 轨迹 2D 预览」；按 S0-7 先做 GLB 原型 |
| P1-13 | `RobotModel::loadFromUrdf` 第 53~83 行；`fanuc_m10ia.json`；`buildSeamFromPickedEdge` 第 73~74、89~97 行；`ICadKernel::tessellate` / `evaluateFaceOutwardNormal` | `loadFromUrdf` 先拿硬编码模型，再用正则覆盖关节限位，正则依赖属性顺序；JSON 无人读取，且 `joint_offsets_deg` 全为 0，与 C++ 的 −90°/180° 矛盾；跨零件匹配成功但找不到第一面时，静默使用伪造的默认法向 `(0,0,1)`；`tessellate` 和 `evaluateFaceOutwardNormal` 没有任何调用方；`chordToleranceMm`、`maxVel/maxAcc` 没人用 | §4.3「派生数据可重算」与 M05「参数单一真值」；未接线字段会让读者误以为能力已具备 | 删除或接线；找不到面时返回错误；URDF 用真 XML 解析，并据此拟合或校验 OPW |
| P1-14 | `.claude/contexts/OLP-archive.md` §0.2、`README.md`、`SessionBrief.md` | 记忆文件把不存在的模块写成已建成（见矩阵 #42）；README 写着 Three.js、ROBOGUIDE 对拍、「与真机 100% 对齐」；SessionBrief 写「正在搭建」，README 写「全部通过」 | `CLAUDE.md` 要求接手者先读这些文件，虚报会直接误导下一阶段 | 刷新为与代码一致的描述，未完成项明确标注 |

### 4.3 P2（10 条）

| ID | 位置 | 问题 | 建议 |
| :--- | :--- | :--- | :--- |
| P2-1 | `WorkcellLayoutService::defineUFrameByThreePoints`（返回 `doc.sceneNodes.back()` 的引用）；`CollisionAndSimEngine::kinematics_`（`const&` 成员）；命令持有 `ProjectDocument&` | 生命周期隐患。g++ 13 在 `src/main.cpp:51` 报 `-Wdangling-reference` | 返回 id 或 `std::optional`；成员改为值或 `shared_ptr` |
| P2-2 | `CommandManager::executeCommand` / `ModifyTargetPoseCommand` | 目标 id 不存在时仍压入一条空命令（`probe_app` S8b）；合并没有会话边界，两次独立编辑会被合成一条撤销（`probe_more` M4）；`isDirty` 没有保存点，撤销回到保存时状态仍为脏 | `execute` 返回成功与否；显式的合并会话；记录 clean index |
| P2-3 | `Transform4d::fromThreePoints` | 三点共线或重合时静默生成零轴坐标系 | 检查退化并返回错误 |
| P2-4 | `OcctCadKernel::importStep` | `std::stoi/stod` 异常未捕获；每行跑 4 个 `std::regex`，大文件不可用 | 换 OCCT 后自然消失；过渡期加异常捕获 |
| P2-5 | `HtmlAnimationExporter` | `program.name`、`node.name` 未转义直接写进 JS/HTML | JSON 转义 |
| P2-6 | `ICadKernel.h` 第 70 行，`SeamAndToolpathAlgo.cpp` 第 80 行，`CollisionAndSim.cpp` 第 105、259 行 | 跨零件公差 2.0 mm（指导文档与 Plan 为 <1.0 mm）；L 段跳变阈值 25° 且是加权范数（Plan 为 15°）；扫描闭区间产生 73 点，含 ±180° 重复；`pointToFaceBoxDistance` 是近似距离 | 统一到文档阈值并写成配置项 |
| P2-7 | 分层与目录 | M07/M08 放在 `olp_algo`（Plan 为 `src/app/seam`、`src/app/toolpath`）；`MainWindow` 持有具体类 `OcctCadKernel`；`olp_tests` 链接 `olp_ui`；工件/夹具角色启发式写在 CAD 内核里；目录是扁平的，Plan 规定了子目录 | 阶段 2 前定一种布局，统一 Plan 和代码 |
| P2-8 | 构建与路径 | `vcpkg.json` 没有 `builtin-baseline`，CMake 也没接 toolchain；`main` 把输出路径写死到源码树的 `build/` | 固定 baseline 与 toolchain；输出路径做成参数 |
| P2-9 | `Plan.md` 第 19、49~55、62、113、312 行等 | 实体数 12/11/13 不一致，含不存在的「安装法兰过渡台」；「5 个真实工件 STEP」里有 1 个合成件；`olp_io` 纯 C++ 与 `QPdfWriter` 矛盾；遗漏 M12 中间表示、§9.3 工期弹性和若干风险条目；文中 `file:///c:/...` 是 Windows 本地链接 | 下一次计划刷新时修正，并对 A3/A7 补修订记录或恢复原文 |
| P2-10 | `.claude/Rules.md` | 仍把视觉系统写成当前主线；Plan 形态违反「1~3 项」；`.claude/Plan.md` 有繁体字 | 记忆刷新 |

---

## 五、测试有效性审计（20 条断言实测全部通过）

| 测试 | 声称（README / 测试名） | 实际测到的 | 结论 |
| :--- | :--- | :--- | :--- |
| Test 1 WPR | 万向锁与往返精度 | `fromWPR → toWPR → fromWPR` 矩阵往返；公式经代数核对正确 | 有效，但没有外部样本 |
| Test 2 URDF/STL | 「验证 URDF 关节限位与 OPW 参数」 | `loadFromUrdf` 内部先调 `createFanucM10iA()`，`c2=600`、`J1=2.967` 都是硬编码默认值；STL 只断言三角形非空 | **同义反复**；URDF 改坏了也会通过；米制问题测不出 |
| Test 3 OPW 100 组 | 「100 组对拍，与真机对齐」 | 同一模型下 FK→IK→FK；选解时把原关节角当作「上一点」，还原是必然的；J5 刻意避开奇异 | **自反闭环**；测不出 P0-1、P0-2 |
| Test 4 方案 A | 「基座对齐偏差 0 mm」 | 合成长方体包围盒底面中心对 `(0,0,500)` | **同源真值**；不检查朝向，也不检查替换后机器人与外形是否叠合 |
| Test 5 T 型焊缝 | 跨零件识别、角平分线、豁免、夹具碰撞 | 平面盒子上角平分线正确（有效）；豁免通过是因为豁免了所有工件；把 TCP 放进夹具包围盒内必然判碰撞 | 部分有效；豁免测试掩盖了 P1-5 |
| Test 6 Gizmo/撤销 | 拖拽一条记录、100 步一致 | 单个目标的 `pos.z`；没覆盖宏命令、反向、合并、重新生成后撤销、多目标 | 有效但很窄 |
| Test 7 `.ls` | 「金样本结构」 | 子串查找；CONFIG 是默认值 | **不是金样本**；测不出 P1-11 |

---

## 六、做得好的

1. **WPR 与万向锁实现正确。** `Transform4d::toWPR` 按 §6.4 实现；`w=0, r=atan2(−r12, r22)` 对 `p=±90°` 两种情况都成立（本审计代数验证）。Test 1 是少数「测试命题等于实现命题」的测试。
2. **OPW 的核心数学是可挽救的。** 8 组解的分支（前/后、肘上/下、腕翻转）、腕部奇异分支、球腕 ZYZ 分解都与自身 FK 严格互逆；接上符号与偏置后，与 URDF 的偏差为 0.000000 mm（P0-1 探针）。P0-1 是接线问题，不需要重写算法。
3. **§6.1 焊缝坐标系纯函数正确。** `computeSeamFrame` 与 `applyWeldingAngles` 的构造顺序、退化检查与指导文档一致。
4. **数据模型骨架对得上 §4.3。** `TargetRole`、`ProcessEvent`、`poseInUFrame`、`userModified` / `userDeltaOffset`、`TargetCache`、`FanucConfig`、`ControllerUnit → Program → Operation → Target` 都在，替换内核时不必推翻文档结构。
5. **位姿命令的主路径是对的。** 拖动预览不写文档，松手一条命令，Esc 不入栈；`MacroCommand` 按逆序撤销；`ModifyTargetPoseCommand` 在合并后的偏移累积 `D0·P0⁻¹·P2` 数学上自洽，撤销和重做都能精确复原。
6. **焊接段配置锁的意图已经落地。** WELD_START 锁定配置，段内 `forbid=true`；L 插补逐步保持配置并做跳变检测，符合 §6.2、§6.3 的方向（细节见 P1-7）。
7. **`.ls` 文本骨架与指导文档示例同构。** 分段、`UFRAME_NUM`/`UTOOL_NUM`、`Arc Start` 写在运动行之后、`WELD_SPEED`、FINE/CNT、`/POS` 保留 3 位小数、程序名校验都对。
8. **Plan 守住了几条关键边界。** P2 没有进 P0，3D PDF 五天熔断和 HTML 保底完整保留，TcpGizmo「不挂接」写成了硬规范。
9. **算法层与 Qt 物理隔离。** `src/algo`、`src/io` 没有任何 Qt 依赖（虽然现在全仓库都没有 Qt）。

---

## 七、进入下一部分前必修清单（按顺序）

1. **运动学（P0-1、P0-2、P1-8）**：接上 `q = s·couple(J) − o`；FK 和 IK 对称实现；法兰系与 URDF 的 `tool0` 对齐。以 URDF 链为金样本加测试（零位 + 1000 组，<0.01 mm）。耦合系数、圈数轴、F/N·U/D·T/B 判据、FANUC 世界坐标原点四项标「待核」，并进入 S0-4。
2. **几何内核与单位（P0-3、P0-4）**：接入真 OCCT（XCAF、`TopExp`、`BRepAdaptor_Curve` + `GCPnts_AbscissaPoint`、`BRepLProp_SLProps`、`BRepExtrema_DistShapeShape`、`BRepMesh_IncrementalMesh`）；没有 OCCT 时导入失败。用 4 个真实 STEP 建回归（面数和边数等于文件实体数）。STL 米→毫米由机器人加载器显式处理。
3. **命令栈（P0-5）**：所有 `ProjectDocument` 写入改为 `ICommand`；仿真只写缓存；补最小的选择集与变更通知；空命令不入栈；合并显式分会话。
4. **构建与表现层（P0-6）**：产品配置下 Qt6、OCCT 为 `REQUIRED`，另设显式 `OLP_HEADLESS`；做出最小真视口（拾取 + `AIS_Manipulator` 取增量）；接入 GoogleTest 与 CI。
5. **门禁与如实描述（P0-7、P1-14）**：测试按命题改名；9 项验收单列为「未执行」；`Compile.bat` 和 `main` 失败时返回非零；README 与记忆文件如实刷新。
6. **阶段 0 外部门禁（P0-8）**：执行 S0-3、S0-4、S0-5，记录 S0-6 熔断决策；向客户索取 2~3 份真实带机器人场景，复核 3.4 方案 A（在此之前 3.4 状态为「暂定」）。
7. **已提前实现的阶段 2~4 代码二选一**：要么隔离为 experimental 并从 README 撤下宣称；要么先修静默错误：P1-1 末端漏焊、P1-2 单帧无收弧、P1-3 手改错配、P1-4 反向后焊速错误、P1-5 豁免集合、P1-10 Gizmo 参考系。
8. **Plan 文本修正（P2-9、矩阵 #6~#8、#10、A3、A7）**：恢复验收 A3、A7 原文或补修订记录；解决 `olp_io` 与 `QPdfWriter` 的矛盾（建议 2D 报告单独成库，例如 `olp_report`）；修正场景实体数。

第 1~6 项完成，并且 S0-4、S0-5 拿到外部数据之后，才建议进入阶段 2。

---

## 八、与 PR #1 旧审计的主要分歧

旧审计：`Docs/AUDIT_Part1_Plan_and_Framework.md`，分支 `cursor/audit-part1-framework-3999`。本审计在形成全部结论后才阅读它，以下逐条重新举证。

**一致的部分**：框架不合格，不建议进入阶段 2；`OcctCadKernel` 不调用 OCCT；零位 FK 与 URDF 相差 1067.8 mm；STL 米制；命令栈被服务层绕过；测试自洽；HTML 是 2D canvas；`Compile.bat` 吞掉测试失败；单帧焊缝缺 `Arc End`（旧审计先发现，本审计已复现）；A3 搭接焊被替换。

**分歧：**

1. **3.4 方案 A 拍板。** 旧审计判「严格一致」；本审计判**部分偏离**。拍板依据 `Full_Welding_Cell_With_Robot.step` 由项目自己生成（文件头 `StepBrepBuilder` / `MimoVR`，12 个长方体）。指导文档 §12.6 与 `Docs/SM.txt` 要求客户提供 2~3 份真实场景并说明来源软件。验收 A7 也从「至少 2 份真实整场景」弱化为这一个合成文件；旧审计只注意到 A7 追加的 `<0.01 mm` 内控，并判为可接受。建议把拍板降级为「暂定」。
2. **焊丝尖端豁免的修法。** 旧审计建议「只豁免 `id == activeWorkpieceId`」。本审计不同意：T 型跨零件焊缝的焊丝尖端同时贴着立板和底板，只豁免单个工件，会让每个焊接点在另一块板上误报碰撞，正是 R08 要防的情况。正确做法是豁免集合取焊缝两侧母材，只在 `wireTipExemptionRadiusMm` 内生效，数据模型从单个 `targetWorkpieceId` 扩展为集合。
3. **阶段 0/1 的执行顺序。** 旧审计认为「S0-2 提前是排期细化，只是状态标记过宽」；本审计判为**实质偏离，并且是根因**：在 ROBOGUIDE 对拍、金样本、OCCT 拾取之前铺开了 M06、M12 及阶段 2~4 代码，违反 §0.5，运动学约定错误就是这样被固化的（P0-8）。
4. **J2/J3 耦合的性质。** 旧审计只把它当作「Plan 在对拍前写死系数」的文本问题；本审计证明代码中耦合方向与 FK 约定不一致（只动 J2 10°，前臂转 20°），在任何 ROBOGUIDE 结论下都是错的，单列为 P0-2。旧审计对 P0-1 只给出零位偏差；本审计进一步证明接上已存在但未用的符号/偏置字段后偏差为 0，指明了根因和修法。
5. **Plan 内部矛盾。** `olp_io` 被列为纯 C++，而 M13 的 `QPdfWriter` 又放在 `olp_io` 中，旧审计未指出（矩阵 #10）。旧审计判 M12「严格一致」，本审计认为漏了指导文档的 P0 项「品牌无关中间表示」。
6. **旧审计漏掉的静默错误**（均有探针数据）：焊缝末端漏焊 10~40 mm（P1-1）；路径反向后整条焊缝以 200 mm/s 焊接（P1-4）；手改 APPROACH 的偏移被套到 WELD_START（P1-3）；仿真覆盖用户锁定配置（P0-5）；自转扫描碰撞维度恒为 13.0 mm（P1-6）；`main` 演示程序自身 2 处碰撞照样导出（P0-7）；STEP 单位换算依赖行序（P0-4）；4 个真实 STEP 的拓扑丢失量化（P0-3）；方案 A 替换后机器人与外形相差约 1 m（P1-9）；`Compile.bat` 的配置失败检测因 `%ERRORLEVEL%` 解析期展开而失效（P0-7）；`OLP-archive.md` 虚报不存在的模块（P1-14）。
7. **事实更正。** 旧审计写「非 PLANE 面……法向保持默认零向量」。实际默认值是 `(0,0,1)`（`CadFaceInfo::outwardNormal{0.0, 0.0, 1.0}`，`probe_more` M6 实测）。这比零向量更隐蔽：它是合法的单位向量，不会触发 `computeSeamFrame` 的退化检查。
8. **对齐度评级。** 旧审计判 Plan 对齐度「高」；本审计判「中高」，理由是上面第 1、3 点和 A3、A7 两条越权改动。

---

## 附录：复现方法与关键数据

- **构建**：`cmake -S . -B /tmp/audit-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++`，然后 `cmake --build /tmp/audit-build -j8`。只有一条告警：`src/main.cpp:51 -Wdangling-reference`。`olp_tests` 输出 `Summary: 20 passed, 0 failed.`
- **`main`**：12 实体，隐藏 5 个，基座 `(0,0,500)`，烘焙 748 帧，节拍 37.3394 s（焊接 35.225 s / 空走 2.11438 s）。`build/` 不存在时仍打印「Exported」并退出 0，文件并未生成。
- **探针**（仓库外编译，链接 `/tmp/audit-build/libolp_*.a`；源码和输出在 artifacts 的 `audit_probes/`）：
  - `probe_kin`：K1 零位对比；K2 1000 组 URDF 对比（原样 1508.928 mm，接上符号/偏置后 0.000000 mm）；K3 J2/J3 联动（20.000° 对 0.000°）；K4 同一物理姿态的关节角与限位判定；K5 J5 圈数。URDF FK 按 `fanuc_m10ia_standalone.urdf` 逐关节复合：`Trans(origin)·Rot(axis, q)`，其中 J3/J5 绕 −Y，J4/J6 绕 −X。
  - `probe_app`：S1 焊缝末端覆盖；S3 校验状态；S4 自转扫描；S6 反向前后的 `/MN`；S8a/S8b/S8d 命令与 Gizmo 边界；S9 重新生成时手改错配。S5、S7、S8c 三项因探针设计问题由 `probe_more` 的 M2、M3、M4 取代，其中 S8c 受 C++ 函数实参求值顺序影响，输出不可用。
  - `probe_more`：M1 演示程序碰撞来源；M2 非当前工件豁免；M3 锁定配置被覆盖；M4 合并跨会话；M5 方案 A 叠合偏差；M6 真实 STEP 解析；M7 单位行序；M8 STL 单位。
  - `probe_short`：20 mm 短焊缝生成的 `.ls` 缺少 `Arc End`。
- **标注「待核」的条目**（需 ROBOGUIDE 或 FANUC 手册确认）：J2/J3 耦合系数正负；配置字圈数对应轴（代码 J1/J4/J6，FANUC 默认通常 J4/J5/J6）；F/N 判据在 FANUC J5 符号下的取向；FANUC 世界坐标原点是否位于 J2 轴高度。本报告对这些项只断言「代码未核且与可推导的约定不一致」，不断言最终正确值。
