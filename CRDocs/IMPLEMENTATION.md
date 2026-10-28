# M11-PLAYBACK-SHAPE-001 — Round 2：URDF 连杆链驱动外形与停止后清理 TCP 预览（DIR-001 第 5 步，M11-005）

> 执行者：执行方（Executor）
>
> 日期：2026-10-28
>
> 轮次：Round 2
>
> 状态：**READY_FOR_REVIEW**

根据审核方在 Round 1 审核中指出的两项 P1 问题（`M11-007` 与 `M11-008`），执行方经独立核验与数学推导，确认原实现确实误将 OPW 抽象连杆帧（连杆 4 位于腕心、连杆 6 对齐 tool0）套到了在 URDF 连杆系下建模的 `link_k.stl` 上，且时间轴面板在停止后通过 `publish(false)` 重新绘制了起点帧。本轮针对这两项缺陷进行了彻底整改与闭环：

---

## 本轮核心修改

### 1. `M11-007`：机器人 7 节外形全面切换至 URDF 连杆链驱动

- **问题根因核验**：
  - `SampleScenes` 中的 `base_link.stl` 至 `link_6.stl` 均在标准 URDF 连杆坐标系下建模；
  - OPW 算法中的 `fk.linkTransforms` 为纯运动学计算的 D-H / OPW 抽象帧：连杆 4 原点被置于腕心（偏离 URDF 连杆 4 原点达 640 mm），连杆 6 姿态对齐 tool0 法兰（相对 `link_6.stl` 旋转了 180°）；
  - 碰撞检测引擎（FCL）所采用的 `forwardRobotLinkWorldFrames` 正是依据 URDF 真实关节链依次累加 `origin` 与带 J2/J3 耦合的旋转轴向量构建的真实外形位姿。
- **改动落实**：
  1. **轨迹烘焙统一**（`src/algo/simulation/TrajectorySimulator.cpp`）：
     - 引入 `olp/algo/FclCollisionQueries.h`；
     - 在 `sampleTrajectory` 中，烘焙帧 `linkWorldTransforms` 彻底改用 `forwardRobotLinkWorldFrames(joints, plan.robotBaseInWorld)` 生成，不再使用 OPW `fk.linkTransforms`。
  2. **视口场景位姿统一**（`src/ui/OcctViewport.cpp`、`src/ui/OcctViewportPlayback.cpp`）：
     - 在 `syncScene()` 与 `syncRobotPosture()` 中，场景初始及姿态同步时的 `sceneRobotLinkTransforms` 均改用 `forwardRobotLinkWorldFrames(joints, localTransform)`；
     - 在 `FclCollisionQueries.cpp` 中将 `axisRotation` 移出 `#ifdef OLP_HAS_FCL`，使 `forwardRobotLinkWorldFrames` 无条件计算完整的三维旋转矩阵。
  3. **无头模式真实创建 OCCT AIS 图元并挂接变换**（`src/ui/OcctViewport.cpp`、`src/ui/OcctViewportPlayback.cpp`）：
     - 在 `syncScene()` 中，只要存在机器人节点，即通过 STL 加载器构建 `s_robotLinkFaces[0..6]`，并在 `view_->robotLinkAis[0..6]` 中真实生成 `AIS_Shape` 句柄，赋予初始 `SetLocalTransformation(toGpTrsf(sceneRobotLinkTransforms[k]))`；
     - 在 `setPlaybackRobotTransforms` 与 `clearPlaybackPreview` 中，即使 `context.IsNull()`（无头测试环境），也必须直接对 `view_->robotLinkAis[k]` 调用 `SetLocalTransformation`；
     - `playbackRobotTransforms()` 直接从 `view_->robotLinkAis[k]->Transformation()` 反解出世界变换矩阵，确保回归测试验证的是 OCCT 图元自身的实际呈现位姿，杜绝只测试数据成员的伪通过。

### 2. `M11-008`：停止播放后彻底清除 TCP 球与轨迹，停留在场景关节姿态

- **问题根因核验**：
  - `TimelinePanel::stopPlayback()` 在调用 `clearPreview_()`（清理 TCP 预览并恢复场景连杆姿态）后，紧接着执行了 `publish(false)`；
  - `publish(false)` 会采集起点帧（0.0 s）并调用 `showFrame_`，导致停止后视口中 TCP 红色/黄色球重新出现，且机器人被拉到程序起点位姿，破坏了“停止回归场景初始姿态”的行为规范。
- **改动落实**：
  1. **时间轴面板停止逻辑重构**（`src/ui/TimelinePanel.cpp`）：
     - 在 `TimelinePanel::stopPlayback()` 中，彻底移除 `publish(false)` 调用；
     - 停止时依次执行：`timer_->stop()`、`cursor_.stop()`、`clearPreview_()`（清理 TCP 球、轨迹并复位 7 节连杆到场景位姿）、重置滑块归零（`setValue(0)`）、以及在有计划时调用 `updateReadout()` 更新文字标签为 0.00 s；
  2. **接口暴露供测试验证**（`include/olp/ui/TimelinePanel.h`）：
     - 将 `stopPlayback()` 提升为 public 成员函数，供测试直接调用验证。

### 3. 自动化测试套件针对性断言升级 (`tests/test_simulation.cpp`)

- **Home 姿态 7 节连杆 URDF 吻合度断言**：
  - 在 Test 20 中，对场景初始化后的 7 节连杆分别比对 `playbackRobotTransforms()` 与 `forwardRobotLinkWorldFrames(home, base)`：
    - 遍历 0～6 节连杆，位置误差均 $< 1.0\text{ mm}$，姿态角距离均 $< 1.0^\circ$；
    - 针对性核验连杆 4 位置：实测误差为 `0.000000 mm`（$< 1.0\text{ mm}$，彻底消除了 OPW 640 mm 的腕心偏移）；
    - 针对性核验连杆 6 姿态：实测误差为 `0.000000 deg`（$< 1.0^\circ$，彻底消除了 180° 反转问题）。
- **播放时跟随烘焙帧断言**：
  - Seek 采样中段帧，调用 `setPlaybackRobotTransforms` 后核验 7 节连杆均与 `activeFrame.linkWorldTransforms` 严格一致（位置 $< 10^{-3}\text{ mm}$，姿态 $< 10^{-3\circ}$）。
- **清理复位断言**：
  - 调用 `clearPlaybackPreview()` 后核验 7 节连杆均回归初始 Home 姿态（位置 $< 10^{-3}\text{ mm}$，姿态 $< 10^{-3\circ}$）。
- **停止播放不重画 TCP 断言**：
  - 构造 `TimelinePanel`，挂接钩子函数并调用 `stopPlayback()`；
  - 断言 `clearPreview` 被成功触发；
  - 断言 `showFrame` 未被触发（`!hookFrameCalledOnStop`），验证 TCP 球绝不重新画回。

---

## 边界与风险保持（RISK-001）

- **`RISK-001` 维持 OPEN**：
  - J2/J3 耦合比（`+1.0`）及圈数计算（J1、J4、J6）保持不变，在未接入真实 ROBOGUIDE 或物理机器人对拍前继续保持 OPEN；
- **第 6 到第 7 步严格未做**：
  - 未动工程保存 JSON（第 6 步）；
  - 未动 3D PDF 悬置收口（第 7 步）。

---

## 验证与测试证据

### 1. 编译构建

使用 MSVC 14.44.35207 工具链完整构建：
```powershell
cmake --build build-full --config Release -- /p:VCToolsVersion=14.44.35207
```
**构建结果**：Exit code 0，所有目标编译成功无任何报错。

### 2. 自动化测试套件全量通过

运行完整测试套件：
```powershell
cmd.exe /c "set PATH=D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;%PATH% && build-full\Release\olp_tests.exe"
```
**测试输出摘录**：
```text
  [PASS] M11-007: Initial link 0 pos matches URDF chain (< 1 mm)
  [PASS] M11-007: Initial link 0 rot matches URDF chain (< 1 deg)
  [PASS] M11-007: Initial link 1 pos matches URDF chain (< 1 mm)
  [PASS] M11-007: Initial link 1 rot matches URDF chain (< 1 deg)
  [PASS] M11-007: Initial link 2 pos matches URDF chain (< 1 mm)
  [PASS] M11-007: Initial link 2 rot matches URDF chain (< 1 deg)
  [PASS] M11-007: Initial link 3 pos matches URDF chain (< 1 mm)
  [PASS] M11-007: Initial link 3 rot matches URDF chain (< 1 deg)
  [PASS] M11-007: Initial link 4 pos matches URDF chain (< 1 mm)
  [PASS] M11-007: Initial link 4 rot matches URDF chain (< 1 deg)
  [PASS] M11-007: Initial link 5 pos matches URDF chain (< 1 mm)
  [PASS] M11-007: Initial link 5 rot matches URDF chain (< 1 deg)
  [PASS] M11-007: Initial link 6 pos matches URDF chain (< 1 mm)
  [PASS] M11-007: Initial link 6 rot matches URDF chain (< 1 deg)
  [PASS] M11-007: Home link 4 pos matches URDF within 1 mm (not OPW 640 mm wrist offset)
  [PASS] M11-007: Home link 6 posture matches URDF within 1 deg (not OPW 180 deg flipped)
  [PASS] M11-005: Playing link 0 pos follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 0 rot follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 1 pos follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 1 rot follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 2 pos follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 2 rot follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 3 pos follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 3 rot follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 4 pos follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 4 rot follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 5 pos follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 5 rot follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 6 pos follows baked frame linkWorldTransforms
  [PASS] M11-005: Playing link 6 rot follows baked frame linkWorldTransforms
  [PASS] M11-005: clearPlaybackPreview resets link 0 pos to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 0 rot to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 1 pos to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 1 rot to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 2 pos to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 2 rot to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 3 pos to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 3 rot to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 4 pos to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 4 rot to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 5 pos to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 5 rot to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 6 pos to scene posture
  [PASS] M11-005: clearPlaybackPreview resets link 6 rot to scene posture
  [PASS] M11-008: stopPlayback() invokes clearPreview to clear TCP sphere and restore scene posture
  [PASS] M11-008: stopPlayback() does not publish start frame, preventing TCP sphere reappearance
...
====================================================================
  Summary: 657 passed, 0 failed.
====================================================================
```
**结果**：全量测试套件 657 项测试 100% 通过（0 失败），退出码 0。

### 3. 端到端工作流验证

```powershell
cmd.exe /c "set PATH=D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;%PATH% && build-full\Release\SiFastSuiteOLP.exe --verify"
```
**结果**：退出码 0，5 步完整离线编程工作流顺利生成 `WELD_001.ls` 与 `WELD_001_Preview.html`。

### 4. Rules.md 1000 行限制核验

统计本轮涉及的全部源文件实际行数：
- `include/olp/device/RobotModel.h`：**75 行**（`< 1000`）
- `include/olp/ui/OcctViewport.h`：**145 行**（`< 1000`）
- `include/olp/ui/TimelinePanel.h`：**61 行**（`< 1000`）
- `src/algo/FclCollisionQueries.cpp`：**421 行**（`< 1000`）
- `src/algo/simulation/TrajectorySimulator.cpp`：**575 行**（`< 1000`）
- `src/device/RobotModel.cpp`：**297 行**（`< 1000`）
- `src/ui/OcctViewport.cpp`：**640 行**（`< 1000`）
- `src/ui/OcctViewportPlayback.cpp`：**187 行**（`< 1000`）
- `src/ui/OcctViewportPrivate.h`：**88 行**（`< 1000`）
- `src/ui/ShellWindow.cpp`：**812 行**（`< 1000`）
- `src/ui/TimelinePanel.cpp`：**285 行**（`< 1000`）
- `tests/test_kinematics.cpp`：**182 行**（`< 1000`）
- `tests/test_simulation.cpp`：**488 行**（`< 1000`）

全部文件行数均严格控制在 1000 行限制以内。

---

## 交付文件清单

| 文件 | 修改性质 | 说明 |
|---|---|---|
| `src/algo/FclCollisionQueries.cpp` | 修改 | `axisRotation` 移出 FCL 条件编译，使 URDF 链无条件解算 3x3 空间旋转姿态 |
| `src/algo/simulation/TrajectorySimulator.cpp` | 修改 | 解决 `M11-007`：烘焙帧连杆变换改用 `forwardRobotLinkWorldFrames` 生成真实 URDF 连杆位姿 |
| `src/ui/OcctViewport.cpp` | 修改 | 解决 `M11-007`：场景位姿采用 URDF 连杆链解算；无头模式下依然完整构建 7 节 `AIS_Shape` 几何图元 |
| `src/ui/OcctViewportPlayback.cpp` | 修改 | 解决 `M11-007`：`setPlaybackRobotTransforms` 即使在无头模式也更新 `AIS_Shape` 的局部变换；`syncRobotPosture` 采用 URDF 链 |
| `include/olp/ui/TimelinePanel.h` | 修改 | 暴露 `stopPlayback()` 公共方法 |
| `src/ui/TimelinePanel.cpp` | 修改 | 解决 `M11-008`：`stopPlayback()` 移除 `publish(false)`，清理预览后不再重新绘制起点帧 TCP 球 |
| `tests/test_simulation.cpp` | 修改 | 增加针对 `M11-007`（连杆 4 误差 $< 1\text{ mm}$、连杆 6 姿态 $< 1^\circ$、全臂 7 节位置与姿态）与 `M11-008`（停止不重绘）断言 |
| `CRDocs/STATUS.md` | 修改 | 轮次切为 Round 2，状态置为 `READY_FOR_REVIEW`，所有者设为审核方（Reviewer） |
| `CRDocs/IMPLEMENTATION.md` | 覆盖更新 | Round 2 实施交付报告与测试证据 |
