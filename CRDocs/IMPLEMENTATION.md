# M10-COLLISION-REACH-001 — 机器人运动学可达性、奇异点检测、碰撞检测与自转角扫描（实现与验证交接）

> 执行者：Gemini / Claude
>
> 日期：2026-10-26
>
> 轮次：Round 1
>
> 状态：READY_FOR_REVIEW（见 `STATUS.md`）
>
> 对应任务：M10 机器人运动学可达性、奇异点检测、碰撞检测与自转角扫描（关节限位与 5° 警戒区判定、腕/肘/肩奇异点与可操作度计算、FANUC 构型一致性与 ConfigJump 拦截、焊丝尖端豁免在焊接与非焊接角色对比、72 组自转角扫描可视化组件与一键寻优微调、问题列表联动导航）。

---

## 1. 核心功能与架构实现

### 1.1 MainWindow 验证与扫描管线 (`MainWindow.h` / `MainWindow.cpp`)
- **操作验证管线**：
  - 新增 `validateOperation(const std::string& opId)` 与 `validateCurrentOperation()`；
  - 接入 `CollisionAndSimEngine`，针对操作所有焊接/进退避目标点执行逆解、FANUC 构型一致性、关节软限位、腕/肘/肩奇异点及可操作度评估；
  - 自动将评估结果写回 `TargetCache`（`status`、`solvedJointsRad`、`solvedConfig`、`minClearanceMm`、`manipulability`、`issueDescription` 等）；
  - 操作生成（`generateOperationFromSelectedSeam`）、姿态平滑（`smoothTargetOrientations`）、自转角微调（`rotateTargetsTcpZ`）以及工艺参数变更（`modifyTargetsProcessParams`）后自动触发 `validateOperation`，保持文档状态与 UI 实时联动；
- **自转角 72 组扫描接口**：
  - 新增 `scanSelectedTargetTwistAngles(double stepDeg = 5.0)` 接口，对选中的目标点沿焊枪本地 Z 轴在 $[-180^\circ, +175^\circ]$ 范围内进行 72 组采样；
  - 调用 `CollisionAndSimEngine::scanTwistAngles`，评估每个角度下的逆解可达性、碰撞安全间隙与奇异点/限位警告。

### 1.2 自转角可视化微调控件 (`ShellWindow.h` / `ShellWindow.cpp`)
- **`TwistScanWidget`（方案 6.5 自转角圆环/表盘控件）**：
  - 继承 `QWidget`，以甜甜圈圆环（Donut Ring）划分为 72 个扇区（每扇区 $5^\circ$）：
    - **绿色扇区**：逆解可达、无碰撞、无奇异点/限位警告；
    - **黄色扇区**：逆解可达，但处于警戒区（间隙低于安全阈值、接近关节限位 $< 5^\circ$ 或接近奇异点）；
    - **红色扇区**：不可达（无同构型逆解）、超限或发生碰撞；
  - 表盘中心显示当前自转角数值及状态摘要；
  - 包含白色指针与角度标记线；
  - 支持鼠标悬停展示当前扇区角度、碰撞间隙及状态提示（Tooltip）；
  - 支持鼠标点击扇区直接触发 `angleSelected(double angleDeg)` 信号；
  - 提供 `findOptimalFeasibleAngleDeg()`：自动扫描最大连续可行安全区间（绿色且间隙最大），返回中心最优可行自转角。
- **一键微调按钮与属性面板集成**：
  - 在属性面板自转角面板中嵌入 `TwistScanWidget`；
  - 新增 `★ 一键微调至最优自转角` 按钮：点击后调用 `findOptimalFeasibleAngleDeg()` 计算全局最优角，通过 `controller_.rotateTargetsTcpZ` 提交撤销重做栈，并自动刷新；
  - 选中目标点时，属性面板自动展示构型字串（`N U T, 0, 0, 0`）、Yoshikawa 可操作度指标、各轴关节角及 `TwistScanWidget`。

### 1.3 问题列表联动与视口/表格同步 (`ShellWindow.cpp`)
- **`issueList_` 联动导航**：
  - `fillIssues()` 解析仿真与验证问题，为列表项注入 `targetId`、`opId` 及严重级别（红/橙色区分阻断性错误与警告）；
  - 连接 `issueList_->itemClicked` 与 `itemDoubleClicked`：
    - 自动通过 `controller_.selectOperation(opId)` 与 `controller_.selectTarget(targetId, opId)` 选中目标操作与点；
    - 视口相机手柄挂载至该目标点，3D 机器人姿态联动更新至该点的计算逆解；
    - 路径点表格（`targetTable_`）高亮并滚动定位至对应行。

---

## 2. 自动化测试套件与验证（Test 17）

在 `tests/test_main.cpp` 中新增 `testKinematicsReachabilitySingularityAndTwistScan()`：
1. **关节软限位与 5° 警戒区验证**：
   - 验证 Home 姿态各轴无超限且不处于警戒区；
   - 验证 J1 在 $168^\circ$（限位 $170^\circ$）处于警戒区但未超限，在 $175^\circ$ 判定为超限；
   - 验证 J2 在 $-88^\circ$ 触发下限警戒区，在 $-95^\circ$ 判定为超限。
2. **运动学奇异点与可操作度验证**：
   - 验证 Home 姿态远离奇异点且可操作度 $> 0.95$；
   - 验证腕奇异点（J5 = $0^\circ$）：成功检出腕部轴 4 与轴 6 共线奇异，可操作度降至 0；
   - 验证肘奇异点（前臂伸直 $q_2 + \psi = 0$）：成功检出肘部奇异；
   - 验证肩奇异点与可操作度有界性（严格属于 $[0, 1]$）。
3. **构型一致性与 ConfigJump 拦截**：
   - 验证多候选逆解在构型锁定时准确选出与当前构型一致的解；
   - 验证当给定姿态无法保持构型时返回空解，防止直线段发生姿态突跳（`ConfigJump`）。
4. **焊丝尖端碰撞豁免与夹具干涉判定**：
   - 验证焊接角色下焊丝尖端接触母材不产生误报（`checkTorchPoseCollision` 返回 false）；
   - 验证非焊接角色或非豁免工件在相同位置报出接触/近距碰撞；
   - 验证焊枪喷嘴碰到夹具 `Fixture_Clamp_Left` 时严格报警，并准确返回碰撞部件名。
5. **72 组自转角可行性扫描与最优角算法验证**：
   - 验证 `scanTwistAngles` 产生严格 72 组采样，跨越 $[-180^\circ, +175^\circ]$，可达扇区占比正常；
   - 验证 `TwistScanWidget::findOptimalFeasibleAngleDeg` 在连续可行区间 $[-30^\circ, +50^\circ]$ 中准确命中几何中点 $+10^\circ$。
6. **MainWindow 与交互联动验证**：
   - 验证焊缝生成操作后自动缓存仿真结果、验证目标点缓存且状态非 `Unchecked`；
   - 验证通过控制器接口扫描选中目标点自转角返回 72 组有效样本。

---

## 3. 构建与验证证据

### 3.1 自动化测试全量通过
```powershell
$env:PATH = "D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;$env:PATH"; .\build-full\Release\olp_tests.exe
```
**测试输出**：
- 334 passed, 0 failed（全绿）。

### 3.2 真实几何模型与端到端验证通过
```powershell
$env:PATH = "D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;$env:PATH"; .\build-full\Release\SiFastSuiteOLP.exe --verify
```
**输出结果**：
- [1/5] 导入工作站 STEP 模型并执行机器人替换；
- [2/5] 提取 T 型接头交叉焊缝与角平分线姿态；
- [3/5] 生成刀轨与工艺事件（切入切出避让夹具 32mm）；
- [4/5] 执行逆解、碰撞检测与动画帧烘焙（764 帧，无阻断性错误）；
- [5/5] 导出 FANUC .ls 与 HTML 离线预览；
- 退出码：0。
