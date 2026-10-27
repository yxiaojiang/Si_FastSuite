# M11-TRAJECTORY-001 — Round 3：补上负侧跨过 +180°

> 执行者：Executor
>
> 日期：2026-10-27
>
> 轮次：Round 3
>
> 状态：APPROVED（已闭环，见 `STATUS.md`）
>
> 本轮只处理仍为 OPEN 的 `M11-001`。没有改写 `REVIEW.md`。`M11-002` ~ `M11-006` 仍在 `DEFERRED.md`。

---

## 问题

Round 2 的圈数回退用 `selectBestSolution` 的原始距离选最近解。J4 从 −170° 走向 +170° 时，正确候选折在 +180°，相对已展开的负角原始距离接近一圈。最近解是另一套手臂姿态，被丢掉。折点记成 `Linear segment has no inverse solution; motion was not interpolated`，该采样离开直线。

## 修改

文件：`src/algo/simulation/TrajectorySimulator.cpp`、`tests/test_simulation.cpp`。

- 锁定圈数找不到解时，不再按原始距离回退。
- 在翻转、肘、肩相同且未超关节限位的候选里，按短程增量的平方和取最近的一个。
- 取到之后仍走 Round 2 的同一次 15° 判断。超过 15° 时问题句仍是 `Linear segment joint step exceeds 15 degrees`。
- 关节运动的原始差值、直线采样的短程写入、段末对齐，都没有改算法。

## 未改

- `src/algo/CollisionAndSim.cpp` 未改。
- `src/algo/OpwKinematics.cpp` 未改。逆解主值和圈数定义保持原样。
- `M11-002` ~ `M11-006` 未做。
- `REVIEW.md` 未改。

## 验证

```powershell
$env:PATH = "D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;" + $env:PATH
.\build-full\Release\olp_tests.exe
```

**425 passed, 0 failed**，退出码 0。Round 2 的 419 项仍通过。新增覆盖：J6 从 −179° 到 +179°，J4 从 −170° 到 +170°，J4 从 +170° 到 −170°。三条都不报无逆解，不置 `hasConfigJumpInLinear`，相邻关节步小于 15°，每个采样的正运动学落在该点球面插值上（位置 0.5 mm、姿态 1° 以内）。原有 J6 从 179° 到 −179° 的用例还在。

```powershell
.\build-full\Release\SiFastSuiteOLP.exe --verify
```

退出码 0。`M11 baked 1161 frames | Estimated cycle 38.5067 s (Weld: 33.305 s, Air: 5.20174 s).` 与 Round 2 相同。

## 异议与理由

无。
