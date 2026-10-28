# M15-TJOINT-REGRESSION-001 — Round 1：结束 3D PDF 悬置（收口 RISK-003）与 SampleScenes T 型接头闭环自动化回归测试（DIR-001 第 7 步，M13/M15）

> 执行者：执行方（Executor）
>
> 日期：2026-10-28
>
> 轮次：Round 1
>
> 状态：**READY_FOR_REVIEW**

根据已拍板的 `PlanDocs/DECISIONS.md` DIR-001 第 7 步及用户指令，执行方已全面完成：
1. 用一段话结束 3D PDF 的悬置，正式熔断收口 `RISK-003`；
2. 针对 `SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_TJoint_With_Clamps.step` 建立完整端到端自动化回归测试（Test 23）。

---

## 一、 结束 3D PDF 悬置（RISK-003 熔断定调收口）

### 1. 定调定案说明
在设计与风险文档（`CRDocs/DEFERRED.md`、`.claude/SessionBrief.md`、`.claude/Plan.md`、`Plan.md`）中统一定调并正式收口 `RISK-003`：

> **【RISK-003 熔断定调收口】** 经 DIR-001 拍板定案，正式结束 3D PDF 的五天熔断悬置状态：明确不再投入资源自研高脆弱度且受限于 Adobe Reader 专有沙箱环境的开源 U3D/PRC+JavaScript 动画嵌入链路；MVP 离线三维可交互仿真交付统一收口至基于 Web 标准的「单文件离线 HTML 3D 动画包（Three.js + GLB 关键帧烘焙）」，2D 报告由已投产的 QPdfWriter 承载。若后续特定客户有强需求，直接转入商业 SDK（如 HOOPS Publish / Tech Soft 3D）技术选型评估，不在内部开展开源逆向或重度定制。至此，`RISK-003` 正式收口（CLOSED）。

### 2. 文档状态同步
- `CRDocs/DEFERRED.md`：`RISK-003` 状态由 `[OPEN]` 流转为 `[RESOLVED in M15]`（CLOSED 熔断收口）；
- `.claude/SessionBrief.md`：从未关闭风险列表中移除 `RISK-003`，第 7 步记录为已就绪待审；
- `.claude/Plan.md`：M2 里程碑达成度表中“3D PDF 五天熔断”与“T 型接头走同一条闭环”均标记为「完成」；
- `Plan.md`：文首刷新最新进展，Active Sprint 补充第 7 步收口记录。

---

## 二、 Scene 1 T 型接头端到端自动化回归测试（Test 23）

独立新建测试单元 `tests/test_tjoint_regression.cpp`，完整覆盖 5 大关键环节：

### 1. 工位导入与装配校验（Workcell Station Import & Setup）
- 自动解析加载 `SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_TJoint_With_Clamps.step`；
- 严格断言成功识别 4 个独立 CAD Solid 及其碰撞角色：
  - `Base_Plate_400x300x12` (`CollisionRole::Workpiece`)
  - `Vertical_Web_Plate_400x12x160` (`CollisionRole::Workpiece`)
  - `Fixture_Clamp_Left` (`CollisionRole::Fixture`)
  - `Fixture_Clamp_Right` (`CollisionRole::Fixture`)
- 挂载 `FANUC M-10iA` 6 轴弧焊机器人模型，核验 OPW 几何契约（$a_1=150, a_2=-150, c_1=450, c_2=600, c_3=640, c_4=100$ mm）；
- 定义 150 mm 焊枪工具 `UTOOL 1`（Z=+150 mm），工件整体配置为基座前方舒适工作区 `UFRAME 1`（原点置于 $(750, 0, 300)$ mm）。

### 2. 跨零件边拾取与 T 型角焊缝识别（Cross-Part Edge Picking & Seam Recognition）
- 拾取立板底边 Edge 3（400 mm 跨实体贴合边，跨越立板与底板）；
- 调用 `SeamAndToolpathAlgo::buildSeamFromPickedEdge`；
- 断言跨实体侧面识别：`seam.face1PartId != seam.face2PartId`（`face1=part_2`, `face2=part_1`）；
- 断言焊缝长度覆盖 400 mm 跨度，采样点弧长严格单调递增。

### 3. 焊枪双侧平分线求解与焊角姿态标架（Torch Dual-Side Bisector & Seam Frame Solving）
- 对全部采样点逐点核验母材两外法向 $n_1$（立板外侧 $(0, -1, 0)$）与 $n_2$（底板顶面 $(0, 0, 1)$）的正交性（$|n_1 \cdot n_2| < 10^{-4}$，精确 90° 直角）；
- 断言角平分线 $b = (n_1 + n_2)/\|n_1 + n_2\|$ 与理论 45° 空间角严格匹配（全采样点最大角度偏差 $< 0.01^\circ$）；
- 检验局部焊缝标架正交右手系行列式 $\det(R) \approx 1.0$；
- 叠加焊接工艺行走角（$10.0^\circ$）与工作角。

### 4. 焊丝尖端豁免机制验证（Wire-Tip Collision Exemption）
- **焊接状态下豁免生效**：在 `isWeldingRole = true` 且母材零件加入 `exemptionPartIds` 时，焊丝尖端 Sphere 0（半径 2 mm，$\le 10$ mm 豁免阈值）与母材贴合，`checkTorchPoseCollision` 判定无干涉碰撞（`collided == false`），杜绝起弧点虚警；
- **非焊接状态下豁免失效**：在 `isWeldingRole = false` 时，相同位姿准确报出与底板的物理接触碰撞（`collided == true`），确保空走转移安全；
- **夹具保持严格碰撞检测**：将焊枪置于左压紧夹具 `Fixture_Clamp_Left`（$X \in [-190, -140], Y \in [-140, -70], Z \in [12, 55]$）内部或近侧时，严格报出干涉且准确指示 `hitPart = "Fixture_Clamp_Left"`。

### 5. 避障与轨迹生成（Obstacle Avoidance, Trajectory Simulation & .ls Export）
- **夹具避障与起弧内缩**：调用 `extendStartTrimUntilTorchClears`，检测到起弧端近夹具时自适应内缩起弧点，确保焊枪全包络距离夹具满足安全间隙（$\ge 5$ mm）；
- **Home 转移安全过渡**：调用 `insertSafeTransitBeforeApproach` 检验机械臂从 Home 位姿至切入点过渡不与夹具/工件发生干涉扫掠；
- **全轨迹运动学仿真**：调用 `validateAndSimulateProgram`，各目标点 OPW 逆解全部有效锁定，直线焊接段无配置字跳变（`hasConfigJumpInLinear == false`），无关节限位越界；
- **仿真烘焙**：`TrajectorySimulator::plan` 生成 30 FPS 运动关键帧（烘焙帧数 $> 0$），节拍时间（焊接时间与空走时间）严格非零且正向单调；
- **FANUC .ls 后处理导出**：`FanucLsPostProcessor::validateProgram` 校验通过，`generateLsString` 输出包含标准 `/PROG`、`/ATTR`、`/APPL`、`/MN`、`Arc Start[1]`、`Arc End[1]`、`/POS`（WPR 姿态与配置字）、`/END` 的合法程序，并落盘至 `build/TJOINT_001.ls`。

---

## 三、 风险与约束遵守情况

1. **`RISK-001` 维持 OPEN**：J2/J3 耦合比（`+1.0`）及圈数计算保持不变，不依赖未安装的 ROBOGUIDE；
2. **`RISK-002` 维持 OPEN**；
3. **`RISK-003` 已正式收口（CLOSED 熔断收口）**；
4. **编译严格带 `/p:VCToolsVersion=14.44.35207`**；
5. **单文件行数限制（1000 行）严格遵守**：
   - `tests/test_tjoint_regression.cpp`: 299 行（$< 1000$）
   - `tests/test_support.h`: 34 行（$< 1000$）
   - `tests/test_main.cpp`: 67 行（$< 1000$）
   - `CMakeLists.txt`: 221 行（$< 1000$）
   - `CRDocs/STATUS.md`: 27 行（$< 1000$）
   - `CRDocs/DEFERRED.md`: 351 行（$< 1000$）

---

## 四、 验证证据

### 1. 编译命令与输出
```powershell
cmake --build build-full --config Release -- /p:VCToolsVersion=14.44.35207
```
**结果**：Exit code 0，全部 C++20 目标构建通过。

### 2. 自动化测试套件
```powershell
cmd.exe /c "set PATH=D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;%PATH% && build-full\Release\olp_tests.exe"
```
**结果**：**752 passed, 0 failed**，退出码 0（含既有 725 项测试 + 新增 27 项 T 型接头回归测试全部通过）。

### 3. 产品启动与验证命令
```powershell
cmd.exe /c "set PATH=D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;%PATH% && build-full\Release\SiFastSuiteOLP.exe --verify"
```
**结果**：Exit code 0。
