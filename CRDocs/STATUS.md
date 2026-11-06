# 当前协作状态

- **Task ID**：`VE-DEMO-001-REAUDIT`
- **Task**：Vision Edge 页首演示（VE-D1..D7，Mode 2 Mock）独立复审
- **Round**：`1`
- **Status**：`APPROVED`
- **Last Updated By**：独立审核（只更新 CRDocs；未改生产代码）
- **Blocking Issues**：`0`（无 OPEN P0/P1）
- **Summary**：
  1. **复审（2026-11-06 UTC）**：对照 `Docs/Vision_Edge_System_Baseline.md` 页首 2026-10-22 收窄范围与 `PlanDocs/DECISIONS.md` VE-DEMO-001。实现落在 `olp_runtime`，复用 OPW IK 与 FANUC `.ls`，不另起第二套 OLP，也未把真空/蘸漆/羽化/完整 HMI 拉进本期。
  2. **一键安全门槛成立**：`runVisionEdgeDemoOnce` 强制 `gateMockOnIkAndLsPass=true`。独立探针：不可达轮廓走完整状态机 → Fault，且 upload/run/executeTrajectory 次数均为 0；IK 已通过但程序名非法 → LS Failed 且 Mock 两步 Skipped。未门控 D6 调试通道在 IK 失败时仍会笛卡尔执行，保持 `VE-D6-001` 的既有口径。
  3. **A/B**：同一 `runVisionEdgeDemoOnce`；摆放 A/B 轨迹几何可区分（探针起点约 `(1007.5,650,791)` 与 `(973.7,747.3,791)`）；`mockDemoPassed` 且 `claimsSiteSafe=false`。
  4. **门禁**：`olp_runtime_tests` **211 passed, 0 failed**，exit 0。本环境无 Qt6 / OpenCASCADE / FCL，**未**跑 `olp_tests`，**未**做现场或真机验收。
  5. **新 P2** `VE-D7-001` OPEN（不阻断本门禁）：`runProgram` 失败后仍可能调用 `executeTrajectory`，编排层再把 `executedMock` 写成 false。`MockRobot` 在 upload 成功后不会走这条失败。
- **下一代码任务**：无阻断项。`VE-D7-001` 在接入会失败 `runProgram` 的适配器、或要把 `executedMock` 当作「是否发生过笛卡尔运动」的审计字段时，由执行者 B 按 `DEFERRED.md` 修复。

## 本轮门禁

| 项 | 结论 |
| :--- | :--- |
| 范围 VE-D1..D7 / Mode 2 | 符合页首演示；未扩张到 v1.1 真空/蘸漆/HMI/现场验收 |
| 状态机失败 → Fault | 通过（含不可达轮廓的 once 全路径） |
| ik/ls 阻断一键 Mock 执行 | 通过（关闭 VE-D6-001 的一键路径） |
| AB 同逻辑、两次轨迹 | 通过，且几何不同 |
| FCL / Mock 口径 | 诚实：`NotBuilt` / `NotCheckedNoFcl`，`boundToFcl` 被强制 false，`claimsSiteSafe` 恒 false，`.ls` 带 RISK-001 水印 |
| DeviceResult | 坏 JSON、空路径、不可达 once 不抛异常 |
| `olp_runtime_tests` | 211/211 |
| Qt / `olp_tests` / FCL / 真机 | **未验证**（环境不具备） |
| OPEN P0/P1 | 无 |
| 本期模拟 | **APPROVED（Mock / headless only）** |

**不等于**真机下发、现场视觉、板件 FCL Cleared、GUI/`olp_tests` 全绿、或 RISK-001 关闭。
