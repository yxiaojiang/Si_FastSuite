# PROPOSAL — VE-DEMO-001 Vision_Edge 演示闭环分阶段计划（Agent A）

## 要决定的问题

在 `vision-edge` 分支上，如何把 2026-10-22 收窄后的「一块木板两次涂胶」演示闭环拆成可验收阶段，供本地三智能体（A 审计划/放行，B 实现，C 审实现）推进；不得按已作废的 v1.1 全量 125~145 人天扩张。

## 建议决定

### 唯一设计权威与范围

1. **执行权威**：`Docs/Vision_Edge_System_Baseline.md` **页首** 2026-10-22 收窄条款（「本期验收」「本期不做」「报价 20261022-3」），以及 `.claude/contexts/VisionEdgeSystem.md` 文首同口径。
2. **技术参考（非本期验收）**：Baseline v1.1 正文（M01~M08、S0 红线、6 板型、真空托台、蘸漆站、完整 HMI/归档、亚毫米指标）——客户认可演示后再单独立项。
3. **明确非权威**：`Docs/Vision_Glue_Integration_Assessment.md` v0.1（未走 PlanDocs 采纳）；`Vision_Edge_System_Final_Review.md` 仅解释 v1.1 如何收敛，不扩大本期范围；OLP 主线 `Docs/OLP_System_Design_Claude.md` 仍是 OLP 权威，Vision Edge **复用其资产**，不另立第二套 OLP 基线。
4. **本期唯一验收（真机有硬件时）**：机器人与相机位姿固定；小木板平放桌面；一键开始→找板→沿边涂胶；平移并转动后再开始，不示教、不改程序仍成功；胶在侧边且目视连续即通过。
5. **本期明确不做**：多种板型、真空小托台、亚毫米计量、阶段 0 三条红线作为演示门禁、蘸漆站与羽化、各向同性浮动试验、完整 HMI 与运行归档。板直接放桌面；软辊、慢速、防滑垫减推移。
6. **无真机/相机时的软件验收口径**：只验收接口、Mock 回放、合成轮廓两次摆放闭环、规划/校验/程序产物与回归测试；**禁止**把 Mock 成功写成真机验收通过。

### 技术路径（演示级）

- **落点**：在现有 C++/Qt/OCCT OLP 仓库内新增 `src/vision/`（感知桥接，不依赖 Qt）与 `src/runtime/`（编排与设备通信），遵守「新建类前先归并」「1000 行拆分」「脚本纯英文」。
- **模式**：演示必须体现「找板+沿边」，走**简化模式 2（无 CAD 现拍现算）**：轮廓→（可选）瞬态棱柱→沿边偏置轨迹→校验→下发/Mock 执行。模式 1（已知 CAD+位姿）仅作可选加速对照，**不替代**演示主路径。
- **复用**：OPW/FCL/仿真烘焙/`FanucLsPostProcessor`/视口回放/无头测试框架。演示允许密集 L+CNT 顶替 C 运动；滚轮工具几何必须数据化，不得继续写死焊枪球。
- **设备**：`ICamera` / `IRobot` / `IIO` + Mock；真实驱动仅在硬件到位后接同一接口。本仓库当前 `include/olp/device` 仅有运动学 `RobotModel`，设备运行时抽象为新建。
- **语言**：演示感知可用「合成轮廓/录制数据集」先不引入 Python 侧车；若后续接真实点云再评估侧车（Assessment 建议，未采纳，不强制本期）。

## 证据

- Baseline 页首 L3~L13：收窄演示、v1.1 改扩展、20261022-3 报价、本期不做清单。
- `VisionEdgeSystem.md` L3~L5：同期权威口径。
- Assessment L3~L8、L193~L206：性质为未采纳独立评估；演示级模式 2 约 54~79 人天【估算】，不得当作本期排期权威。
- 仓库：`vision-edge` @ `94d860d`（= origin/main），工作区干净；无 `src/vision`/`src/runtime`；`OLP_HEADLESS` 与 `olp_tests` 已存在；`DIR-001/002/003` OLP 资产可复用；`RISK-001` 仍 OPEN（无真机对拍）。
- `PlanDocs/STATUS.md` 上一题 `DIR-003` IDLE；SessionBrief 仍指向 OLP `DIR-002` P0-B——Vision Edge 在本分支开新 Topic，不偷改 OLP 已 APPROVED 的 CRDocs 任务。

## 未核实

- 现场机器人品牌/选项、相机型号（Q1/Q2）——演示软件阶段用 Mock，真机阶段再绑。
- 本 box 为 Linux，完整 Qt/OCCT Windows 构建与安装包验证依赖用户 Windows 机；阶段验收以可移植无头测试为主，Windows GUI 为加分项。
- OCCT `MakePrism`/`MakeOffset`、FCL Cylinder 在本仓 OCCT 8 / FCL 0.7 下的编译可用性——阶段 3/4 实测。

## 建议顺序（演示 7 阶段）

见 Agent A 放行清单 VE-D1~VE-D7。总口径对齐「演示级」，人天量级参考 Assessment 演示档（约数十人天），**禁止**按 v1.1 阶段 0~4 / 125~145 人天排期。

## 明确不做（本 Topic）

v1.1 S0-2/4/5 红线实测、真空吸附互锁、蘸漆分段羽化、6 种 CNC 板 30 次成功率、PySide6 完整 HMI、亚毫米合同指标、协作臂新逆解、Stream Motion、模式 3 逐点补偿、另起 Python 独立工程替代 OLP 内核。

## 对上一轮审核的处理

无（新 Topic）。
