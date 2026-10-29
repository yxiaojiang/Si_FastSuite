# CROSS-AUDIT — FIX-EXPORT-GATE-001..004（独立协调方交叉审计）

> **Auditor**：Grok Bot（executor subagent，独立于 `Si_FastSuite_Audit_X`）  
> **Date**：2026-10-29 ~21:40 Asia/Singapore (UTC+8)  
> **Repo**：`/workspace/Si_FastSuite`（working tree，未提交累计 diff）  
> **Scope**：`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md` 项 1–4 累计产品/测试改动  
> **Authority**：设计 `Docs/OLP_System_Design_Claude.md` v1.1（后处理/导出相关已 skim）  
> **Constraint**：未改产品代码；未改 `CRDocs/REVIEW.md` / `STATUS.md`  
> **Compile/test**：本交叉审计 **未在 box 复跑**（与 Audit_X 相同限制）。Windows **783/0** 采信 IMPLEMENTATION/STATUS 用户机记录，非本端复验。

---

## Verdict: **PASS_WITH_RISKS**

默认配置下，计划项 1–4 的核心验收链 **成立**：

1. 端点/回写后的 `Collision` / `ConfigJump` → 默认 `validateProgram.ok == false` → `generateLsString` 空 → `exportToFile` 拒写。  
2. 段内硬撞回写 `target.cache.status=Collision` 且保持 `cache.valid`；Near 不再短路后续硬撞检测。  
3. `.ls` UTF-8 中文路径经共享 `utf8ToPath`；水印经 `generateLsString` 进入文件。  
4. `/MN` 首条草稿注释 + 对话框独立红条；`refreshPreview` 不清除红条；`OpwKinematics` 相对 HEAD **零 diff**。

但存在 **计划范围内/强相关的 P1 残留**（Audit_X 在 GATE-004 声称「无 P0/P1 连带缺陷」——**不同意**该绝对表述）。在默认路径上未见 P0 门禁失效；因此不定 FAIL，也不给干净 PASS。

**对协调方**：可以认为项 1–4 功能主体达标；若按计划「完成定义 = 全量审查无 OPEN P0/P1」则 **尚不能关闭本修复计划**，须先处理或正式降级/接受下方 P1。

---

## 1. Evidence table（file:line，相对当前 working tree）

| # | Claim / path | Evidence | Result |
| :--- | :--- | :--- | :--- |
| E1 | 默认 `blockOnCollision` / `blockOnConfigJump` | `include/olp/io/FanucLsPostProcessor.h:15-16` | PASS |
| E2 | Collision → Error（门禁开） | `src/io/FanucLsPostProcessor.cpp:110-117` | PASS |
| E3 | ConfigJump → Error/Warning 分流 | `src/io/FanucLsPostProcessor.cpp:119-128` | PASS |
| E4 | NearCollision / Singularity → Warning | `src/io/FanucLsPostProcessor.cpp:130-146` | PASS |
| E5 | `!ok` → `generateLsString` 空 | `src/io/FanucLsPostProcessor.cpp:211-221` | PASS |
| E6 | 空 body → `exportToFile` false | `src/io/FanucLsPostProcessor.cpp:388-392` | PASS |
| E7 | UI 阻断碰撞默认勾选 | `src/ui/LsExportDialog.cpp:61-62` | PASS |
| E8 | Warning 黄列，非纯绿掩盖 | `src/ui/LsExportDialog.cpp:139-161` | PASS（仅当 `ok==true` 且 issues 非空） |
| E9 | SampleSession 优先 fail-return 拒写 `WELD_001.ls` | `src/ui/SampleSession.cpp:218-240,246-249` | PASS |
| E10 | Near 不短路硬撞；硬撞短路后续扫掠帖 | `src/algo/CollisionAndSim.cpp:274-276,348-379` | PASS |
| E11 | 段内 Unreachable/ConfigJump/Collision/Near 升级写 cache | `src/algo/CollisionAndSim.cpp:14-50,309-310,330-331,364-365,376-377` | PASS（见 OPEN-P1-3 同级 Hard） |
| E12 | `cache.valid` 故意不改 | `src/algo/CollisionAndSim.cpp:47-48` + 注释 | PASS |
| E13 | Test 9：中段硬撞→cache Collision→默认 validate !ok | `tests/test_collision.cpp:184-194` | PASS（断言存在；几何依赖 FCL） |
| E14 | Test 9b：Near→Hard 最终 Collision | `tests/test_collision.cpp:258-280` | **WEAK** — 未强制 `sawNear==true`（见 OPEN-P1-1） |
| E15 | GATE-001/003 默认拒 Collision/ConfigJump/写回形状/活扫掠 | `tests/test_postprocessor.cpp:214-229,249-354` | PASS（锁测存在） |
| E16 | 共享 `utf8ToPath` + CMake 入链 | `include/olp/io/Utf8Path.h:11`, `src/io/Utf8Path.cpp:19-30`, `CMakeLists.txt:123` | PASS |
| E17 | `exportToFile` / `exportControllerPrograms` 用 UTF-8 路径串 | `src/io/FanucLsPostProcessor.cpp:395-402,426-438` | PASS（修了旧 `(path).string()` 窄码页风险） |
| E18 | ProjectSerializer 删本地副本、改调共享函数 | `src/io/ProjectSerializer.cpp:6,803,872`（diff 删除匿名实现） | PASS |
| E19 | `/MN` RISK-001 水印 | `src/io/FanucLsPostProcessor.cpp:236-237,337-340` | PASS；`LINE_COUNT` 用 `mnLines.size()` 自然 +1 |
| E20 | 对话框红条独立于 `statusLabel_` | `src/ui/LsExportDialog.cpp:32-42` vs `114-174`（refresh **零**引用 riskBanner） | PASS |
| E21 | GATE-004 三断言 | `tests/test_postprocessor.cpp:375-376,391-397` | PASS（文案/位置锁测存在） |
| E22 | `OpwKinematics` 未改 | `git diff HEAD -- include/olp/algo/OpwKinematics.h src/algo/OpwKinematics.cpp` → 空 | PASS |
| E23 | `refreshPreview` 不覆盖 `blockOnConfigJump` | `src/ui/LsExportDialog.cpp:115-118` 只写 collision 四字段；`config_{}` 默认 true | PASS（默认保持） |
| E24 | MainWindow 导出不强制重跑仿真 | `src/ui/MainWindow.cpp:538-548` 仅 `exportToFile(*prog,…)` | **GAP**（见 OPEN-P1-2） |

---

## 2. End-to-end traces（怀疑式推演）

### T1 — 中段硬撞 → cache 回写 → 默认 validate → 空 LS

`validateAndSimulateProgram`：端点先写入 `Valid`（清端），扫掠 `sweepCollided` → `postedHardSweepCollision` + `upgradeTargetCacheFromSegmentIssue(..., Collision)`（`CollisionAndSim.cpp:356-365`）。  
后处理只读 `t.cache.status`（`FanucLsPostProcessor.cpp:110-117`）→ Error → `ok=false` → `generateLsString` 返回 `{}`（`:211-221`）。  
**结论**：默认 API 链闭合。依赖「导出前 cache 已被仿真写过」。

### T2 — Near 然后 Hard → 最终 Collision

旧 `postedSweepIssue` 已拆为 `postedNearSweepIssue` / `postedHardSweepCollision`；Near 后仍继续扫，Hard 可升级（`:366-377` 与 `:356-365`）。同级 Hard 用严格 `>` 不降级。  
Test 9b **不要求**观察到 Near（只要求最终 Hard / Collision）——与计划文案「先 Near 再硬撞」不完全对齐。

### T3 — 中文 UTF-8 导出

`exportToFile`：`utf8ToPath` + `ofstream(path)`；`exportControllerPrograms` 拼接 UTF-8 目录串再交给 `exportToFile`（避免 Windows `path::string()` ACP）。单测写 `…/olp_ls_中文路径导出_001/工位导出_WELD_001.ls`。  
**未在 box 实跑**；采信 Windows 780/783 记录。

### T4 — RISK-001 水印 + 红条不被 refresh 清掉

水印为 `addMnLine` 首行，位于 `/MN` 编号循环之前组装、输出在 `/MN` 之后（`:237` + `:337-339`）。  
红条为 `setupUi` 局部 `QLabel`，`refreshPreview` 只动 `statusLabel_` / `codeEdit_`。  
绿通过与黄警告均不清除红条——分层正确（语法通过 ≠ RISK-001 关闭）。

---

## 3. OPEN issues（ranked）

### P0

（无）在 **默认** `LsPostConfig{}` + 已执行 `validateAndSimulateProgram` 的路径上，未发现可导出硬撞/ConfigJump 的门禁击穿。

### P1

| ID | Issue | Why it matters | Suggested disposition |
| :--- | :--- | :--- | :--- |
| **OPEN-P1-1** | Test 9b **不断言** `sawNear==true`；失败信息仅打印 `nearIssueSeen=yes/no`。计划项 2 验收写的是「先 NearCollision 再硬撞」。 | 若某环境 clearance 从未低于阈值、却仍硬撞，测试仍绿——「不短路」语义只被部分锁定。 | 加 `check(sawNear, …)`；或改计划验收措辞并记录。 |
| **OPEN-P1-2** | `MainWindow::exportFanucLs` / `LsExportDialog` / `exportAllPrograms` **不**强制重跑 `validateAndSimulateProgram`。门禁只信 `target.cache`。项 2 回写只在仿真后生效。 | 用户改障碍物/路径后未再仿真就导出 → 中段硬撞可漏拦。命令路径会把 `cache.valid=false`（部分保护），但「valid 仍 true 的旧 Valid cache」仍可能漏。 | FINAL 前至少文档化为已知限制；或导出前强制/提示重仿真（新产品项）。 |
| **OPEN-P1-3** | `blockOnCollision=false` 时，`validateProgram` **完全不**报告 Collision（`if (config.blockOnCollision && …)`）。UI 可取消「阻断碰撞点导出」→ **纯绿「校验通过」+ 可导出硬撞程序**（仍有 RISK-001 水印）。 | 与项 1「禁止仅纯绿掩盖」精神冲突；默认勾选不能消除用户一键卸门禁。 | 卸载门禁时至少降为 Warning 黄列，禁止纯绿。 |
| **OPEN-P1-4** | Hard 同级（`Unreachable`/`Collision`/`ConfigJump`/`JointLimitExceeded`）严格 `>`：先写 ConfigJump 再发现 Collision **不会**把 status 升成 Collision。默认两者皆 Error → 仍拒写。但 API 若 `blockOnConfigJump=false` 且 `blockOnCollision=true`，硬撞可被 ConfigJump 状态「挡住」而不触发 Collision Error。UI 暂无 ConfigJump 开关，故现网默认安全。 | 项间交互脚枪；Audit_X 未标为缺陷。 | 同级升级策略改为「Collision 优先」或合并多 status；至少单测锁该顺序。 |

### P2

| ID | Issue |
| :--- | :--- |
| OPEN-P2-1 | `Singularity` / `NearJointLimit` 落在 Soft tier；段内 Near 可覆盖奇异点文案（二者皆 Warning，安全影响弱）。 |
| OPEN-P2-2 | `exportControllerPrograms` / `MainWindow::exportFanucLs` 无独立水印 `find` 断言（逻辑上必经 `generateLsString`）。 |
| OPEN-P2-3 | SampleSession 拒写 **不删除** 磁盘上旧 `build/WELD_001.ls`，可能留下「看似正式」的陈旧文件。 |
| OPEN-P2-4 | 计划项 3 可选 `test_tjoint_regression` 收紧 **未做**（计划允许）。 |
| OPEN-P2-5 | ProjectSerializer 仍残留 `#include <windows.h>`（utf8 已外提）；无害噪音。 |
| OPEN-P2-6 | RISK-001 运动学符号仍 OPEN（计划明示冻结）——水印/UI 不关闭该风险。 |

---

## 4. Agreement / disagreement with existing `CRDocs/REVIEW.md`（GATE-001..004）

| Topic | Audit_X | This cross-audit |
| :--- | :--- | :--- |
| 项 1–4 各条强制范围实质达标 | APPROVED ×4 | **同意**主体达标 |
| GATE-004 行号（水印、红条、refresh、三断言、默认门禁仍在） | 精确 | **同意** — 已对当前树逐条核对，未见胡报行号 |
| GATE-003 Utf8Path / 锁测行号 | 精确 | **同意** |
| GATE-002 Near 不短路 + Test 9/9b | APPROVED；称 9b 覆盖 Near→Hard | **部分不同意** — 产品逻辑对；**测试未强制 sawNear**，对计划验收偏弱 |
| GATE-001 SampleSession fail-return / 默认门禁 | APPROVED | **同意** |
| GATE-004「强制连带排查 A–F **均无发现 P0/P1 连带缺陷**」 | 宣称无 P1 | **不同意** — 见 OPEN-P1-1..4（尤其 P1-2/P1-3 属于导出安全交互，不应在 FINAL 口径下被写成零 P1） |
| RISK-001 仍 OPEN；不动 OpwKinematics | 正确 | **同意**（哈希/diff 已核） |
| Windows 783/0 采信、box 未复跑 | 双方一致 | **同意限制**；本交叉审计同样 **未**复跑 |
| 可开 FINAL | Audit_X 说可以开 | **可以开本交叉审计**（已开）；若「关闭计划」需先消化 P1 或书面接受 |

**未发现** Audit_X 对 GATE-001..004 **核心通过项**的明显「无证据空话」；主要问题是 **对连带脚枪/测弱项偏乐观**，而非行号造假。

---

## 5. What was NOT checked

- box / 本端 **未编译、未跑 ctest**；未打开真实 `build/WELD_001.ls` 样件。  
- 未在 GUI 中人工点击 `LsExportDialog`（红条/黄列仅静态读码）。  
- 未跑 ROBOGUIDE / 示教器对拍（RISK-001 本就 OPEN）。  
- 未审 HTML 导出器、Opw 数值金样、CommandManager 工程卫生、cadHandle/dirty（计划冻结）。  
- 未对 `Si_FastSuite_Audit_Report*.md` 或更早 M 里程碑 REVIEW 做全历史审计。  
- 未验证非默认 `sampleStepSec` / 极短段导致 `doSweepCheck` 采样过稀而漏撞（预存离散风险，非本轮引入重点）。  
- 未验证 Qt `QString::toStdString()` 在目标 Windows Qt 版本上的 UTF-8 保证（现代 Qt 文档为 UTF-8；若极端旧工具链则另论）。

---

## 6. Collateral / interaction notes（项间）

- 项 2 回写使项 1 的默认 Collision 门禁能看见中段硬撞 —— 主目标达成。  
- 项 3 锁测重复覆盖项 1/2，并修路径；项 4 在中文路径测上叠加水印断言 —— 叠加合理。  
- 项 4 水印使 MN 行号整体 +1；既有测锁的是 `J P[1:…]` 内容而非 `1: UFRAME`，预期不红（与 IMPLEMENTATION 一致）。  
- SampleSession 在项 2 之后，`blockingIssue && lsVal.ok` 旁路应变稀；代码仍保留该双保险 —— 合理。

---

## 7. Bottom line for coordinator

- **Verdict = PASS_WITH_RISKS**  
- 默认安全拒止主链（仿真写回 → validate → 空 LS / 拒写）+ UTF-8 路径 + RISK-001 草稿门禁：**实质性完成**。  
- **不要**在未处理/未签收 OPEN-P1-1..4 的情况下，按计划「无 OPEN P0/P1」关闭 FIX-EXPORT-SAFETY。  
- Audit_X 分项 APPROVED 可保留；FINAL 口径应吸收本交叉审计的 P1，而非复述「零 P1 连带」。

