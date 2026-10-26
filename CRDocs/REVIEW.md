# 审核结论 — M10-COLLISION-REACH-001

> **Task ID**：`M10-COLLISION-REACH-001`
> **Task**：M10 可达性、奇异点、碰撞与自转角扫描 — Round 1
> **Round**：1
> **Reviewer**：审核方
> **Reviewed at**：2026-10-26
> **Executor**：Gemini / Claude
> **Verdict**：**CHANGES_REQUESTED**
>
> 对照提交 `777091c` 的源码和调用链，不以 `IMPLEMENTATION.md` 自述为完成证据。审核方复跑 `build-full/Release/olp_tests.exe`：**334 passed, 0 failed**。Test 17 覆盖的是引擎直调和“生成后调用 `validateOperation`”，下面三条界面路径它都没有走到。

M09 Round 2 的 `M09-001` ~ `M09-003` 维持 **RESOLVED**。本轮没有把它们打回。

---

## 本轮必须修改

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M10-001 | P1 | **OPEN** | `src/ui/MainWindow.cpp`<br>`src/ui/ShellWindow.cpp`<br>`src/app/CommandManager.cpp` | 平滑、自转、工艺修改在 `execute()` 之前读 `didMutate()`，返回值恒为 false，验证不会跑，状态栏把成功报成失败。 |
| M10-002 | P1 | **OPEN** | `src/ui/MainWindow.cpp`<br>`src/algo/CollisionAndSim.cpp` | 自转扫描锁的是从未写入的默认构型 `N U T, 0, 0, 0`，并且对接近/回退点也按焊接角色豁免焊丝尖端。 |
| M10-003 | P1 | **OPEN** | `src/ui/ShellWindow.cpp`<br>`src/algo/CollisionAndSim.cpp` | 问题列表只读 `target.cache`。段内扫掠碰撞和直线段构型跳变只写在 `SimulationResult.issues` 里，列表中不出现，也无法跳转。 |

`M10-004`、`M10-005`、`M10-006` 已写入 `DEFERRED.md`。不挡这三条，但本轮不能把它们当成已经验收。

---

### M10-001 [P1] 三条编辑入口在执行前读取 `didMutate()`

`SmoothTargetOrientationsCommand`、`RotateTargetAroundTcpZCommand`、`ModifyTargetProcessParamsCommand` 的 `mutated_` 初值是 false，只在 `execute()` 里赋值。`CommandManager::executeCommand` 先 `execute()`，再根据 `didMutate()` 决定是否入栈。

`MainWindow` 把顺序反过来了：

```206:213:src/ui/MainWindow.cpp
    auto cmd = std::make_unique<app::SmoothTargetOrientationsCommand>(
        doc_, opId, startTargetId, endTargetId);
    const bool mutated = cmd->didMutate();
    cmdMgr_.executeCommand(std::move(cmd));
    if (mutated) {
        validateOperation(opId);
    }
    return mutated;
```

`rotateTargetsTcpZ`（约 224–232 行）和 `modifyTargetsProcessParams`（约 247–255 行）相同。命令其实已经入栈并改了文档，`notifyDocumentChanged` 也会刷新界面，但函数永远返回 false，`validateOperation` 永远不跑。

界面按这个返回值报结果：

- 姿态平滑走到失败文案：「未能执行姿态平滑：没有有效操作或中间点」。姿态其实已经插值。
- 扇区点击、一键最优角、±1°/±5°/±15° 和顺/逆时针按钮都包在 `if (rotateTargetsTcpZ(...))` 里。成功提示不会出现。
- 工艺单元格不再显示「已更新目标点工艺参数」。

这和已延期的 `M09-004` 不是一回事。`M09-004` 要求零变化时不要报成功。现在是有变化也报失败。修的时候不能改回“一律返回 true”。

自转还会把 `cache.valid` 清掉（`rotateTargetAroundTcpZ`）。机器人关节只在 `selectTarget` 里、且 `cache.valid` 时才写到机器人节点；同一个点已经选中时 `selectTarget` 直接返回。所以点了最优角之后，手柄跟着新姿态走，手臂仍停在旧关节上，属性里的构型、可操作度和问题说明也不更新。

撤销快照目前只保存了 `cache.valid` 这一个布尔量，没有关节角、构型和问题文本。一旦验证改写了 cache，再撤销会回到旧姿态，关节却还是新解。修这条时必须一起处理。

**验收**

1. 有实际变化时，这三条入口返回 true，并在入栈之后调用 `validateOperation`。零变化仍返回 false，且不入栈。
2. 姿态平滑成功时状态栏不再写“未能执行”。自转和工艺修改成功时写出对应的完成说明。
3. 已选中的点被自转或平滑之后，机器人节点的 `currentJointsRad` 换成新解，不依赖再次切换选中点。
4. 撤销一次自转后，姿态、`userModified` 和 cache（关节、构型、状态、间隙、问题文本）一起回到自转前。重做后再与验证结果一致。
5. Test 17 或相邻测试走 `MainWindow::rotateTargetsTcpZ` / `smoothTargetOrientations`，断言返回值、cache 重新有效、撤销后关节与姿态一致。不要只调用引擎。

---

### M10-002 [P1] 自转扫描锁错构型，接近/回退也豁免焊丝尖端

`scanSelectedTargetTwistAngles` 把 `target->config` 交给 `scanTwistAngles`。全库没有任何写入 `target.config` 的路径。默认值是 `flip=false, up=true, top=true, turns={0,0,0}`，也就是 `N U T, 0, 0, 0`。验证结果写在 `cache.solvedConfig`。

`selectBestSolution` 的第三个参数是 `optional<FanucConfig>`。传入一个构型值就会变成硬锁：`samePosture` 含翻转、肘、肩和三轴圈数。扫描因此始终锁在默认构型上，而不是该点已经解出的构型。参考关节也只在 `cache.valid` 时才用 `cache.jointsRad`；自转刚把 `valid` 清掉，下一次扫描的参考关节退回 Home。

`scanTwistAngles` 里碰撞调用写死 `isWeldingRole=true`。属性面板对任何选中点都扫描，包括 `APPROACH` 和 `RETRACT`。这些点的焊丝尖端插进母材时，圆环会把该角度画成可行。设计里的豁免只在焊接角色、且只对当前焊缝母材、且穿透不超过 `wireTipExemptionRadiusMm`。

Test 17 第 5 段直接把 `computeConfig(home)` 传进引擎，第 6 段只断言样本数是 72。两条都发现不了这条。

**验收**

1. 该点已有解时，扫描锁定 `cache.solvedConfig`（含圈数），参考关节用 `cache.jointsRad`，即使 `cache.valid` 暂时为 false。从未解过的点才用 Home，并且不要锁死默认 `N U T`。
2. 未自转的当前姿态，若验证已经给出同构型解，必须落在可达样本上。构型不是默认 `N U T` 的点也要满足。
3. 碰撞角色跟随点的角色：只有 `WELD_START` / `WELD_VIA` / `WELD_END` 豁免焊丝尖端。同一几何下，接近或回退点尖端进入母材必须是碰撞（红），焊接点在豁免半径内必须不是碰撞。
4. 上述两条用 `MainWindow::scanSelectedTargetTwistAngles` 断言，不要只构造引擎参数。

---

### M10-003 [P1] 段内问题进不了问题列表

`validateAndSimulateProgram` 把端点问题写入 `target.cache`，同时把直线段无解、关节步距超过 15°、扫掠碰撞和扫掠间隙不足只推进 `SimulationResult.issues`。`validateOperation` 把这份结果留在 `lastSimulationResult_`。

`fillIssues()` 只遍历 `target.cache.issueMessage`。端点间隙合格、中段撞夹具的情况（Test 9 已经证明引擎能报出）在问题面板里是空的。单击/双击导航也到不了这些条目。

**验收**

1. 端点清楚、段内硬碰撞或直线段构型跳变时，问题列表出现对应条目，带上 `operationId` 和 `targetId`，硬碰撞/不可达/超限/构型跳变为红色，间隙不足为橙色。
2. 点击该条目会选中对应点，路径点表滚到该行，手柄挂到该点，机器人关节切到该点的解。
3. 回归用一对端点间隙合格、中段穿过夹具的关节运动，断言列表文本里能找到扫掠碰撞，而不是只断言 `SimulationResult.issues` 非空。

---

## 已核对、本轮不打回的部分

- 关节软限位和 5° 警戒：Home 不警戒；J1 = 168° 警戒且未超限，175° 超限；J2 = −88° 下沿警戒，−95° 超限。Test 17 这些断言为真。
- 腕奇异（J5 = 0）可操作度降到 0；肘伸直（\(q_2+\psi=0\)）能检出。Home 的代理指标实测约 0.974。这不是雅可比奇异值，见 `M10-004`。
- 72 个样本的角度是 \([-180^\circ,+175^\circ]\)，步长 5°。\(180^\circ\) 与 \(-180^\circ\) 是同一姿态，不重复采样是对的。
- `findOptimalFeasibleAngleDeg` 对连续可行区间 \([-30^\circ,+50^\circ]\) 取到 \(+10^\circ\)，环绕搜索有 \(2n\) 展开，整圈可行时会截断。
- 生成路径之后会调用 `validateOperation`，cache 不再停留在 `Unchecked`。这条路径没有 `M10-001` 的返回值错误。
- 焊丝尖端豁免在引擎的焊接角色下对母材生效，喷嘴打到 `Fixture_Clamp_Left` 能报出部件名。问题在扫描入口把非焊接点也送进了豁免，见 `M10-002`。
