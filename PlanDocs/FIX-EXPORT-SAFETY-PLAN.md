# FIX-EXPORT-SAFETY — 导出安全门禁修复计划（2026-10-29）

> **独立性**：本文件是本次「Gemini 终审采纳后的安全门禁修复」专用执行计划。  
> **不动原计划**：不改根目录 `Plan.md`、不改 `.claude/Plan.md` 里 DIR-001 / M1–M2 主线表述；本计划不替代 `PlanDocs/DECISIONS.md` 的 DIR-001。  
> **权威依据**：`Docs/OLP_System_Design_Claude.md` v1.1；GeminiResult 终审 + Si_FastSuite 独立裁决（门禁优先，算法冻结）。  
> **协作**：代码改动按 `CRDocs/` 交接；每完成一项 → 编译/相关测试通过 → 独立审核 APPROVED → 才开下一项。全部完成后做一次全量独立审查。

## 目标

默认导出具备真机安全拒止；段内扫掠/插补问题进入后处理可见数据面；RISK-001 只做草稿门禁，不动运动学符号。

## 明确不做（全计划冻结）

- 不改 `OpwKinematics` 的 J2/J3 耦合符号、`offsetsRad`、`signCorrections`、turns 轴映射（RISK-001）。
- 不把 `forwardRobotLinkWorldFrames` 直接换成 `fk.linkTransforms`（OPW 数学标架 ≠ URDF 网格标架）。
- 不改 HTML 导出器架构（不引入 Three.js/GLB）。
- 本轮不拆 `CommandManager.cpp`、不命令化 SampleSession、不修 cadHandle 重建 / openProject dirty（工程卫生，排在安全门禁之后另开任务）。
- 不起弧内缩全臂、BVH 缓存键等 P2。

## 执行项（严格串行）

### 项 1 — 后处理默认门禁（P0-1 + P0-3）

**范围**
- `LsPostConfig::blockOnCollision` 默认改为 `true`。
- 新增 `bool blockOnConfigJump{true}`；为 true 时 `ConfigJump` → `Severity::Error`。
- `NearCollision` / `Singularity` 至少产出 `Warning`（默认可导出，但 UI 不得纯绿「校验通过」掩盖）。
- `LsExportDialog`：`chkBlockCollision_` 默认勾选；存在 Warning 时黄色列出，禁止仅显示纯绿通过文案。
- `SampleSession`：默认配置下若程序含硬碰撞或阻断级问题，不得静默写出可被当真机用的正式 `WELD_001.ls`（失败返回或隔离文件名，二选一，优先失败返回）。

**验收**
- 默认 `LsPostConfig{}` 下，端点 `cache.status == Collision` 的程序：`validateProgram.ok == false`，且 `generateLsString` 为空或等价拒写。
- 默认配置下端点/回写后的 `ConfigJump`：`ok == false`。
- UI 默认勾选阻断碰撞；有 Warning 时界面可见黄色明细。
- 本项相关单元测试通过；全量或至少 postprocessor + 既有回归编译通过。

**非目标**：本项不改 `CollisionAndSim` 扫掠逻辑（属项 2）。

---

### 项 2 — 段内扫掠短路与 cache 回写（P0-2 + D.1）

**范围**
- `postedSweepIssue` 拆为近碰撞与硬碰撞（或等价）：`NearCollision` 不得短路同段后续硬碰撞检测；发现 `sweepCollided` 必须升级记录 `Collision`。
- 段内硬碰撞 / L 段无逆解（`Unreachable`）/ 段内 `>15°`（`ConfigJump`）按严重度优先级回写受影响 `target.cache.status` 与 `issueMessage`（优先级：`Unreachable|Collision|ConfigJump` > `NearCollision` > `Valid`）。
- 不改变「`cache.valid` 表示 IK 解存在」的语义：碰撞点仍可 `valid=true`，由后处理按 status 阻断。

**验收**
- 单测：首尾点 Valid、段中穿过障碍 → `validateAndSimulateProgram` 后默认 `validateProgram` 必须 `!ok`。
- 单测：先 NearCollision 再硬撞的采样序列 → 最终记录为 `Collision`（不得停在 NearCollision）。
- 编译与相关测试通过。

---

### 项 3 — 门禁单测加固与 `.ls` UTF-8 路径（P1-7 部分 + 项1/2 锁测）

**范围**
- `test_postprocessor.cpp`：锁定默认 `LsPostConfig{}` 拒止端点 Collision、段内扫掠 Collision、ConfigJump。
- `FanucLsPostProcessor::exportToFile` 复用与 `ProjectSerializer` 同语义的 `utf8ToPath`（可抽公共工具，避免 Windows 中文路径坏导出）。
- （可选同项）`test_tjoint_regression`：`allTargetsValid` 要求 `status == Valid`；平分线改为有符号点积——若改动导致样例工位现状红测，记入 IMPLEMENTATION 并与审核方确认，不悄悄放宽产品门禁。

**验收**
- 新增/加严断言全绿；中文路径导出单测（或等价）通过；编译通过。

---

### 项 4 — RISK-001 草稿门禁（P0-Gate，不动算法）

**范围**
- 生成的 `.ls` `/MN` 头部插入显式注释：`! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001)`（或同等清晰英文/FANUC 安全注释行）。
- `LsExportDialog` 顶部常驻醒目提示：未完成示教器/ROBOGUIDE 金样对拍，严禁直接作为成品下发真机。
- **禁止**修改 `OpwKinematics` 耦合/turns。

**验收**
- 导出文本含草稿注释；对话框可见提示；运动学相关测试无回归；编译通过。

---

## 每项工作流（强制）

1. 执行者在云端改本项范围代码 + 更新 `CRDocs/IMPLEMENTATION.md`，`STATUS` → `READY_FOR_REVIEW`。
2. 必须证明：**配置/编译成功**，并跑与本项相关的测试（至少 postprocessor / simulation 相关；能跑全量更好）。
3. 独立审核方（`Si_FastSuite_Audit_X`）按 `CRDocs` 审实际 diff 与源码，结论 `APPROVED` 或 `CHANGES_REQUESTED`。
4. 仅当本项 `APPROVED` 后，才开下一项。
5. 四项均 `APPROVED` 后，再开一次**全量独立审查**（覆盖本计划全部 diff），结果汇报用户。

## Task ID 建议

| 项 | CRDocs Task ID |
| :--- | :--- |
| 1 | `FIX-EXPORT-GATE-001` |
| 2 | `FIX-EXPORT-GATE-002` |
| 3 | `FIX-EXPORT-GATE-003` |
| 4 | `FIX-EXPORT-GATE-004` |
| 全量 | `FIX-EXPORT-GATE-FINAL` |

## 完成定义

四项均 APPROVED + 全量审查无 OPEN 的 P0/P1（本计划范围内）→ 本修复计划关闭。原主线 Plan 仍按 DIR-001 / RISK-001 OPEN 继续。

---

## 计划关闭（2026-10-29 Asia/Singapore）

**状态：CLOSED（执行轨结束）**

- 项 1–4：`FIX-EXPORT-GATE-001`..`004` 均 APPROVED  
- 全量：`FIX-EXPORT-GATE-FINAL` 主链 APPROVED（交叉抽核曾标 4×P1，阻塞关闭）  
- 关闭轮：`FIX-EXPORT-GATE-005` APPROVED → OPEN-P1-1..4 **CLOSED**；计划范围无 OPEN P0/P1  
- Windows 最终采信：`SiFastSuiteOLP_AllTests` **800 passed / 0 failed**（含 GATE-005）  
- **主线仍 OPEN**：`RISK-001`（水印≠修运动学）及原 Plan 其他 Open 项  
- 残留 P2（不挡关闭）：含 `exportAllPrograms` 未走 ensure（当前无调用方）等，见 `CRDocs/CROSS_AUDIT_FINAL_COORDINATOR.md` / GATE-005 REVIEW  

本文件此后只作历史基线；新工作另开任务，勿在此继续堆项。

