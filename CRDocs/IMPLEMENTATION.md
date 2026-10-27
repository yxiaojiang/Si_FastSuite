# M12-POST-FANUC-001 — Round 2：闭环修复 M12-001、M12-002、M12-003

> 执行者：Executor
>
> 日期：2026-10-27
>
> 轮次：Round 2
>
> 状态：APPROVED（已闭环，见 `STATUS.md`）
>
> 本轮只处理 REVIEW.md 中仍为 OPEN 的 `M12-001`、`M12-002`、`M12-003`。未改动 `REVIEW.md`。`M12-004` ~ `M12-006` 维持在 `DEFERRED.md`。

---

## 本轮修改与缺陷修复

针对审核方在 Round 1 审核结论中指出的 3 项 P1 缺陷，逐项完成闭环修复与测试用例补齐：

### 1. M12-001 [P1] 速度与 CNT 边界校验与禁止静默改写
- **修改文件**：`src/io/FanucLsPostProcessor.cpp`、`tests/test_postprocessor.cpp`
- **实现细节**：
  - 彻底移除 `generateLsString` 中的静默改写与兜底默认值逻辑（原先 `spd <= 0` 被静默改为 50 或 200，超限 CNT 被静默夹在 0..100）；
  - 在 `validateProgram` 中增加严格的硬性门禁（`Severity::Error`）：
    - 关节运动 `J`：四舍五入至最近整数 `std::round(t.speedValue)`，超出 `1..100%` 范围直接判定为 Error 并拒绝导出；
    - 直线运动 `L`（非 `useWeldSpeed`）：四舍五入至最近整数 `std::round(t.speedValue)`，小于 `1 mm/sec`（如 0.4 mm/sec）直接判定为 Error 并拒绝导出，绝不静默改成 200 mm/sec；有效非焊接直线速度（如 8.6 mm/sec）四舍五入取最近整数 `9mm/sec`；
    - 终止类型 `CNT`：`cntValue < 0 || cntValue > 100` 直接判定为 Error 并拒绝导出，绝不静默夹紧为 `CNT100`；
  - `generateLsString` 严格直接输出校验通过的编程整数值，不再包含任何夹紧或改写。

### 2. M12-002 [P1] 点注释与程序注释超限报错与禁止截断伪通过
- **修改文件**：`src/io/FanucLsPostProcessor.cpp`、`tests/test_postprocessor.cpp`
- **实现细节**：
  - 将 `validateProgram` 中程序注释长度超限（`> config.maxProgramCommentLength`）和目标点注释长度超限（`> config.maxCommentLength`）由原非阻塞 `Warning` 升级为阻断级 `Error`；
  - 彻底移除 `motionLine` 与 `/POS` 中对 `t.name` 调用 `substr(0, config.maxCommentLength)` 的静默截断行为；
  - 超限点名或程序注释在校验阶段直接返回 `ok == false`，`generateLsString` 拒绝导出并返回空字符串；
  - 修正 Test 19 边界测试断言：
    - 精准断言 16 字符程序名合法、17 字符程序名拒绝；
    - 精准断言 16 字符目标点名合法、17 字符目标点名产生 Error 并拒绝导出；
    - 精准断言 32 字符程序注释合法、33 字符程序注释产生 Error 并拒绝导出。

### 3. M12-003 [P1] 关节运动 J 携带 WELD_SPEED 拦截
- **修改文件**：`src/io/FanucLsPostProcessor.cpp`、`tests/test_postprocessor.cpp`
- **实现细节**：
  - 在 `validateProgram` 中新增运动类型与工艺速度匹配校验：当 `t.motion == core::MotionType::J && t.useWeldSpeed` 时，判定为非法组合并记录 `Severity::Error`（提示关节运动必须使用百分比速度 1..100%，不得指定 WELD_SPEED），阻止导出；
  - 在 `generateLsString` 中重构运动指令生成分支：优先判断 `t.motion == MotionType::J` 并强制格式化为百分比速度，杜绝输出包含 `J P[...] WELD_SPEED` 的任何无效语法行；
  - 补齐自动化测试：针对 `J` 运动配置 `useWeldSpeed = true` 的场景断言其校验失败并拒绝导出，断言合法生成文本中绝不包含 `J P[...] WELD_SPEED`。

---

## 验证证据与测试结果

### 1. 自动化全量测试集（`build-full/Release/olp_tests.exe`）

```powershell
$env:PATH = "D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;" + $env:PATH
.\build-full\Release\olp_tests.exe
```

**测试结果**：
```text
====================================================================
  Running SiFastSuite OLP Automated Verification Suite
====================================================================
  ...
--- Test 19: M12 FANUC .ls post-processor validation, events, multi-program export & UI linkage ---
  [PASS] Valid program name 'WELD_001' accepted
  [PASS] Single uppercase char 'A' accepted
  [PASS] Exactly 16-char program name accepted
  [PASS] 17-char program name rejected
  [PASS] Lowercase start 'weld_001' rejected
  [PASS] Digit start '1WELD' rejected
  [PASS] Empty program name rejected
  [PASS] Program name with hyphen 'WELD-001' rejected
  [PASS] Program with no targets reports validation errors
  [PASS] Well-formed program passes validation with no errors
  [PASS] Target name is 16 chars
  [PASS] 16-character target name is accepted
  [PASS] Target name is 17 chars
  [PASS] M12-002: 17-character target name records error and refuses export (not truncated)
  [PASS] Program comment is 32 chars
  [PASS] 32-character program comment is accepted
  [PASS] Program comment is 33 chars
  [PASS] M12-002: 33-character program comment records error and refuses export
  [PASS] M12-001: Joint speed 0% records error
  [PASS] M12-001: Joint speed 0.4% records error and refuses export (not changed to 50%)
  [PASS] M12-001: Joint speed 150% records error and refuses export (not clamped to 100%)
  [PASS] M12-001: Linear speed 0 mm/sec records error
  [PASS] M12-001: Linear speed 0.4 mm/sec records error and refuses export (not changed to 200 mm/sec)
  [PASS] M12-001: Linear speed 8.6 mm/sec formats to nearest integer 9mm/sec
  [PASS] M12-001: Negative CNT value records error
  [PASS] M12-001: CNT 250 records error and refuses export (not clamped to CNT100)
  [PASS] M12-003: Joint motion with useWeldSpeed=true records error and refuses export
  [PASS] blockOnCollision=true rejects program with collided targets
  [PASS] generateLsString generates non-empty .ls text
  [PASS] Header contains /PROG TEST_PROG
  [PASS] Header contains LINE_COUNT
  [PASS] Header contains MEMORY_SIZE
  [PASS] Contains /APPL section
  [PASS] Contains /MN section
  [PASS] MN has J move with speed% and CNT
  [PASS] MN has L move with WELD_SPEED FINE
  [PASS] M12-003: Generated text contains no J moves with WELD_SPEED
  [PASS] MN has Arc Start[2]
  [PASS] MN has Weave Sine[1]
  [PASS] MN has Weave End
  [PASS] MN has Arc End[2]
  [PASS] MN has WAIT instruction
  [PASS] MN has DO instruction
  [PASS] MN has custom comment instruction
  [PASS] Contains /POS section
  [PASS] Contains /END section
  [PASS] emitHomeAsJointTarget outputs J P[HOME] motion instruction
  [PASS] emitHomeAsJointTarget outputs GP1 Joint position block in /POS
  [PASS] Multi-operation outputs first operation separator
  [PASS] Multi-operation outputs second operation separator
  [PASS] Multi-operation point numbering is contiguous across operations
  [PASS] exportControllerPrograms successfully exported 2 .ls programs
  [PASS] PART_A.ls file exists on disk
  [PASS] PART_B.ls file exists on disk
  [PASS] MainWindow::validateProgramForExport validates program
  [PASS] MainWindow::previewFanucLs returns generated text
  [PASS] MainWindow::exportFanucLs writes .ls file to disk

====================================================================
  Summary: 482 passed, 0 failed.
====================================================================
```
用例总数扩增至 **482 项全部通过（0 失败）**。

### 2. 端到端流程校验（`SiFastSuiteOLP.exe --verify`）

```powershell
.\build-full\Release\SiFastSuiteOLP.exe --verify
```

**测试结果**：退出码 `0`，成功导出 `WELD_001.ls` 与 HTML 离线预览，周期 38.5067 秒、烘焙 1161 帧。

### 3. 代码行数规则核验（Rules.md）
- `CommandManager.cpp`: 898 行
- `ShellWindow.cpp`: 880 行
- `OcctCadKernel.cpp`: 797 行
- `SeamAndToolpathAlgo.cpp`: 725 行
- `CollisionAndSim.cpp`: 722 行
- `FanucLsPostProcessor.cpp`: 283 行
- `test_postprocessor.cpp`: 245 行
所有源文件均严格处于 1000 行限制以内。

---

## 异议与理由

无。完全认同审核方 Round 1 意见，速度、CNT、超限注释与关节焊接速度拦截已全部严格落地。
