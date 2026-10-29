# FULL RE-AUDIT — FIX-EXPORT GATE-001..006（协调方独立全盘复审）

> **Auditor**：Grok Bot（executor subagent / 协调方独立复审，非 Audit_X）  
> **Date**：2026-10-29 ~22:09 Asia/Singapore (UTC+8)  
> **Repo**：`/workspace/Si_FastSuite`（working tree 相对 `HEAD` `19cb1ac` 的累计未提交 diff）  
> **Plan**：`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md`（**CLOSED**）+ follow-up `FIX-EXPORT-GATE-005` / `006`  
> **Design**：`Docs/OLP_System_Design_Claude.md` v1.1（M10/M12 相关已对照）  
> **Constraint**：**未改任何产品代码**；本文件为唯一交付物  
> **Compile/test**：box **不能**编译/复跑 ctest。Windows **`SiFastSuiteOLP_AllTests` 805 passed / 0 failed** 仅作协调方/IMPLEMENTATION/**STATUS** 声称采信，**本端未复验**。

---

## Verdict: **PASS_WITH_RISKS**

在 **默认** `LsPostConfig{}` + 写出路径会 `ensure` / 已仿真写回 cache 的主链上，**未发现 P0 门禁击穿**。计划项 1–4 与 GATE-005/006 关闭轮所声称的强制范围，经本轮逐文件核对，**主体成立**：

1. 端点/段内写回的 `Collision` / `ConfigJump` → 默认 `validateProgram.ok == false` → `generateLsString` 空 → `exportToFile` 拒写。  
2. Near 不短路后续硬撞；`sweepCollided` → `Collision` 写回；Hard 同级 `Collision` 优先（`shouldUpgradeCacheStatus`）。  
3. UTF-8 中文路径共享 `utf8ToPath`；`/MN` RISK-001 水印；对话框独立红条 + Warning 黄列。  
4. `exportFanucLs` / `LsExportDialog` / `exportAllPrograms` 导出前 ensure；const validate/preview 在缺仿真时拒。  
5. `OpwKinematics` 相对 HEAD **字节级未改**（哈希一致）；`olp_algo` 无 Qt。

不定干净 **PASS**：仍有安全相邻的 **P2** 残留、主线 **RISK-001**（水印≠修运动学）仍 OPEN、且 **805/0 未在本端复跑**。  
不定 **FAIL**：默认写出主链未见可导出硬撞/ConfigJump 的击穿；先前交叉审计 OPEN-P1-1..4 与 OPEN-P2-GATE005-1 在当前树上 **确已落地关闭**。

---

## 1. Scope covered（累计 GATE-001..006）

| Gate | 主题 | 本轮结论 |
| :--- | :--- | :--- |
| GATE-001 | 默认门禁、UI 勾选、SampleSession fail-return、Near/Singularity Warning | **PASS** |
| GATE-002 | Near 不短路、硬撞写回 cache、`cache.valid` 语义不变 | **PASS** |
| GATE-003 | 锁测 + 共享 `utf8ToPath` + 中文路径导出 | **PASS** |
| GATE-004 | RISK-001 `/MN` 水印 + 对话框红条（不动 Opw） | **PASS** |
| GATE-005 | 关闭 OPEN-P1-1..4（sawNear、ensure/stale、gate-off Warning、Hard 内 Collision 优先） | **PASS（P1 关闭成立）** |
| GATE-006 | 关闭 OPEN-P2-GATE005-1（`exportAllPrograms` per-program ensure） | **PASS（该 P2 关闭成立）** |

计划冻结遵守：未改 Opw 耦合/turns；未换 FK 网格标架；未改 HTML 导出架构；未拆 CommandManager。

---

## 2. Evidence table（file:line，当前 working tree）

| # | Claim | Evidence | Result |
| :--- | :--- | :--- | :--- |
| E1 | 默认 `blockOnCollision` / `blockOnConfigJump` | `include/olp/io/FanucLsPostProcessor.h:15-16` | PASS |
| E2 | Collision 始终上报；gate on→Error / off→Warning | `src/io/FanucLsPostProcessor.cpp:110-120` | PASS |
| E3 | ConfigJump Error/Warning 分流 | `FanucLsPostProcessor.cpp:123-131` | PASS |
| E4 | NearCollision / Singularity → Warning | `FanucLsPostProcessor.cpp:134-149` | PASS |
| E5 | `!ok` → `generateLsString` 空 | `FanucLsPostProcessor.cpp:215-225` | PASS |
| E6 | 空 body → `exportToFile` false | `FanucLsPostProcessor.cpp:393-396` | PASS |
| E7 | `/MN` RISK-001 水印为首条 MN | `FanucLsPostProcessor.cpp:240-242`；`LINE_COUNT`=`mnLines.size()` `:328` | PASS |
| E8 | `exportToFile` / bulk 用共享 UTF-8 路径 | `FanucLsPostProcessor.cpp:399-406,430-442`；`Utf8Path.h:11`；`Utf8Path.cpp:19-30`；`CMakeLists.txt` 入 `olp_io` | PASS |
| E9 | ProjectSerializer 删本地 utf8、改调共享 | `ProjectSerializer.cpp` diff 删匿名实现 + `#include "olp/io/Utf8Path.h"` | PASS |
| E10 | UI 阻断碰撞默认勾选 | `LsExportDialog.cpp:61-62` | PASS |
| E11 | Warning 黄列，非纯绿掩盖 | `LsExportDialog.cpp:143-165`（仅 `ok==true` 且 issues 非空） | PASS |
| E12 | RISK 红条独立；`refreshPreview` 零引用 | `LsExportDialog.cpp:32-42` vs `:114-178` | PASS |
| E13 | Dialog 预览前 ensure | `LsExportDialog.cpp:120-122` | PASS |
| E14 | SampleSession 优先 fail-return | `SampleSession.cpp:218-249` | PASS |
| E15 | Near/Hard 拆旗；硬撞短路后续扫掠帖 | `CollisionAndSim.cpp:282-284,356-386` | PASS |
| E16 | 段内 Unreachable/ConfigJump/Collision/Near 升级写 cache | `CollisionAndSim.cpp:19-27,317-318,338-339,372-373,384-385` | PASS |
| E17 | `cache.valid` 故意不改 | `CollisionAndSim.cpp:26` | PASS |
| E18 | Hard 同级 Collision 优先 | `CollisionAndSim.cpp:37-65`；`CollisionAndSim.h` public API | PASS |
| E19 | `exportFanucLs` 先 ensure | `MainWindow.cpp:647-656` | PASS |
| E20 | const validate/preview 缺仿真拒 | `MainWindow.cpp:551-564,609-620,631-637` | PASS |
| E21 | `exportAllPrograms` per-program ensure | `MainWindow.cpp:669-692` | PASS |
| E22 | Test 9 中段硬撞→cache→默认 validate !ok | `tests/test_collision.cpp:187-194` | PASS（几何依赖 FCL；Windows 声称绿） |
| E23 | Test 9b **强制** `sawNear` | `test_collision.cpp:274-276` | PASS（OPEN-P1-1 已关） |
| E24 | Test 9c Hard 升级矩阵 | `test_collision.cpp:287-308` + `test_main.cpp:48` | PASS |
| E25 | GATE-001/003/004/005/006 锁测 | `test_postprocessor.cpp:214-241,269-374,378-417,504-607` | PASS（断言存在） |
| E26 | `OpwKinematics` 未改 | `git diff HEAD -- …OpwKinematics.*` 空；hash = HEAD | PASS |
| E27 | `refreshPreview` 不覆盖 `blockOnConfigJump` | 只写 collision 四字段；`config_{}` 默认 true（`LsExportDialog.h:29`） | PASS |

---

## 3. End-to-end traces（怀疑式）

### T1 — 中段硬撞 → cache 写回 → 默认拒写

`validateAndSimulateProgram`：端点先评 status；扫掠 `sweepCollided` → `postedHardSweepCollision` + `upgradeTargetCacheFromSegmentIssue(..., Collision)`（`CollisionAndSim.cpp:364-373`）。  
`validateProgram` 读 `t.cache.status==Collision` → Error（默认 gate on）→ `ok=false`（`FanucLsPostProcessor.cpp:112-120,207`）。  
`generateLsString` 空（`:215-225`）→ `exportToFile` false（`:393-396`）。  
**结论**：默认 API 链闭合。写出路径另经 `ensureProgramSimulatedForExport` 强制对当前场景重跑仿真（GATE-005/006）。

### T2 — Near 然后 Hard → 最终 Collision

旧单旗已拆为 `postedNearSweepIssue` / `postedHardSweepCollision`；Near 后继续扫；Hard 可升级。Test 9b **现强制** `sawNear==true`（`:274-276`）+ 最终 cache Collision。  
**结论**：与计划项 2「先 Near 再硬撞」对齐；OPEN-P1-1 关闭证据充分。

### T3 — 用户改几何后导出（OPEN-P1-2 关闭链）

`handleDocumentChanged` 清 `exportSimEnsured_`（`MainWindow.cpp:19`）。有碰撞相关几何且无 frames/未 ensure 时，const validate/preview 拒（`:609-637`）。  
`LsExportDialog::refreshPreview` / `exportFanucLs` / `exportAllPrograms` 均 ensure 后再 validate/写出。  
**结论**：主 UI/写出路径不再盲信陈旧 Valid。残留见 §4 P2-A（文档级 freshness）。

### T4 — 卸载「阻断碰撞」勾选

`blockOnCollision=false` → Collision 仍为 **Warning**（非静默），`ok==true` 可导出；UI 走黄列（`LsExportDialog.cpp:150-157`）。水印仍在。  
**结论**：OPEN-P1-3 关闭成立；与「禁止纯绿掩盖」精神一致。真机风险靠水印 + 用户主动卸门禁。

### T5 — ConfigJump 先于 Collision（OPEN-P1-4）

`shouldUpgradeCacheStatus(ConfigJump, Collision)==true`（`:59-62`）；Test 9c 锁定。默认两者皆 Error → 即使未升级仍拒写。  
**结论**：关闭成立。

### T6 — UTF-8 中文路径 + 水印

`utf8ToPath` + UTF-8 字符串拼接目录；单测写 `…/olp_ls_中文路径导出_001/工位导出_WELD_001.ls` 并断言水印。  
**未在 box 实跑**；采信 Windows 805/0 声称。

### T7 — SampleSession

仿真后 `validateProgram`；`!ok || blockingIssue` → **不写** `WELD_001.ls`，exit 2。HTML 仍可写。  
**残留**：不删除磁盘上旧 `WELD_001.ls`（P2）。

### T8 — `exportAllPrograms`（GATE-006）

对每个 program `id`（空则 `name`）ensure；任一失败则 skip 该 controller bulk。  
**测弱**：ensure 仅在「找不到 program」时失败——遍历 `cu.programs` 时几乎总成功；负路径（ensure-fail→skip）**无锁测**（P2）。

---

## 4. Findings（ranked）

### P0

（无）默认门禁 + ensure 写出路径上未见硬撞/ConfigJump 静默可导出。

### P1

（无 — 本计划范围）先前交叉审计 OPEN-P1-1..4 在当前树均有对应产品/测试落地，本轮复核通过。

### P2（残留，不挡 PASS_WITH_RISKS 主结论）

| ID | Issue | Why it matters |
| :--- | :--- | :--- |
| **P2-A** | Freshness 为**文档级**：`exportSimEnsured_` 或非空 `lastSimulationResult_.frames` 即放行 const validate/preview，**不**按 programId 记账。对 A ensure/validate 后，const 路径可能对 B 给出「非 stale」假象（B 的 cache 未必刚写回）。 | 写出路径 `exportFanucLs`/`exportAllPrograms` 仍 per-program ensure，故不定 P1；API/预览多程序场景可误导。 |
| **P2-B** | GATE-006 未锁 ensure-fail→skip-controller 负路径；该分支对「已在 `cu.programs` 内的对象」几乎死代码。 | 测覆盖缺口。 |
| **P2-C** | SampleSession 拒写**不删除**旧 `build/WELD_001.ls`。 | 陈旧「看似正式」文件残留。 |
| **P2-D** | `Singularity`/`NearJointLimit` 落 Soft；段内 Near 可覆盖奇异文案（皆 Warning）。 | 交叉审计 OPEN-P2-1；安全影响弱。 |
| **P2-E** | 计划项 3 可选 `test_tjoint_regression` 收紧未做（计划允许）。 | 无 diff。 |
| **P2-F** | `ProjectSerializer.cpp` 仍残留 Win32/`windows.h`（utf8 已外提）。 | 卫生噪音。 |
| **P2-G** | 无独立「clipboard / exportControllerPrograms」水印 `find` 断言（逻辑必经 `generateLsString`）。 | 测弱。 |
| **P2-H** | `CollisionRole::Ignored` 节点不触发 freshness；若用户误标夹具 Ignored，门禁变松。 | 角色语义预存风险。 |
| **P2-I** | 主线 **RISK-001**（J2/J3 等）仍 OPEN——水印/红条不关闭运动学风险。 | 计划明示冻结；必须对用户可见。 |

---

## 5. Disagreement with prior REVIEW / CROSS_AUDIT

| Topic | Prior | This re-audit |
| :--- | :--- | :--- |
| GATE-001..004 强制范围 | Audit_X APPROVED ×4 | **同意** |
| CROSS_AUDIT OPEN-P1-1..4 曾阻塞关闭 | PASS_WITH_RISKS；要求关 P1 | **同意当时判断**；**现确认已关闭**（GATE-005） |
| GATE-005「OPEN-P1-1..4 CLOSED」 | Audit_X APPROVED | **同意**（逐条取证） |
| GATE-006「OPEN-P2-GATE005-1 CLOSED」 | Audit_X APPROVED | **同意**产品行为；同意「ensure-fail 负路径测弱」属 P2 |
| GATE-004 曾称「无 P0/P1 连带」 | 过乐观 | **同意交叉审计当时的不同意**；现已由 GATE-005 消化 |
| 「计划范围无 OPEN P0/P1 → 可维持 CLOSED」 | STATUS/计划关闭口径 | **同意无 OPEN P0/P1**；但本复审仍给 **PASS_WITH_RISKS**（P2 + RISK-001 + 未复跑 805/0），**不**写成零风险 |
| Windows 805/0 | 采信 | **同样采信声称，未复跑** |
| Opw 未改 | 多方已核 | **再核哈希一致** |

**未发现** Audit_X 对 GATE-005/006 核心通过项的行号造假；主要提醒是勿把「无 OPEN P0/P1」解读成「导出绝对安全 / freshness 已 per-program 完备」。

---

## 6. What was NOT checked

- box / 本端 **未编译、未跑 ctest**；**未复验** Windows 805/0。  
- 未打开真实 `build/WELD_001.ls` 样件；未在 GUI 点击 `LsExportDialog`。  
- 未跑 ROBOGUIDE / 示教器金样对拍（RISK-001 OPEN）。  
- 未审 HTML 导出器、Opw 数值金样、CommandManager、cadHandle/dirty（计划冻结）。  
- 未验证极端 `sampleStepSec` / 极短段导致扫掠采样过稀漏撞（预存离散风险）。  
- 未验证旧 Qt 上 `QString::toStdString()` 非 UTF-8 的极端工具链。  
- 未对更早 M 里程碑全历史 REVIEW 做审计。

---

## 7. Design alignment（v1.1 摘录对照）

- M12：后处理校验、预览、命名限制、`.ls` 结构 —— 与门禁/水印/预览加强一致；设计强调 ROBOGUIDE 验收 —— 与 RISK-001 草稿门禁一致。  
- M10：离散+段内采样、碰撞/接近两级 —— 与 Near/Hard 拆分及写回一致。  
- 「算法层不依赖 Qt」—— `CollisionAndSim` 无 Qt；UI ensure 在 `MainWindow` —— 符合。  
- J2/J3 耦合需 ROBOGUIDE 核对 —— 计划冻结不动 Opw，仅水印 —— 符合「门禁优先，算法冻结」。

---

## 8. Bottom line for coordinator / Audit_X 交叉

- **Verdict = PASS_WITH_RISKS**  
- FIX-EXPORT 执行轨（含 GATE-005/006 follow-up）在默认安全拒止主链上 **可维持计划 CLOSED**；**无新 P0/P1**。  
- 请 Audit_X 交叉时重点抽核：**P2-A 文档级 freshness**、GATE-006 负路径测弱、SampleSession 旧文件、以及声称的 **805/0** 是否仍成立。  
- 勿重开 `FIX-EXPORT-SAFETY-PLAN.md` 除非发现 P0；P2/RISK-001 走各自轨。

---

*End of FULL_REAUDIT_COORDINATOR_2026-10-29.*
