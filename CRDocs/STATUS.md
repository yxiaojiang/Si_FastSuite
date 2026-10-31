# 当前协作状态

- **Task ID**：`MAINT-CLEANUP-P2-001`
- **Task**：CommandManager 行数门禁治理与 FIX-EXPORT-P2 延期项彻底闭环
- **Round**：`2`
- **Status**：`APPROVED`
- **Last Updated By**：`Reviewer`
- **Blocking Issues**：`0`
- **Summary**：
  1. **P1 `MAINT-001` RESOLVED**：`programCanonicalKey`（`src/ui/MainWindow.cpp:554-562`）一段程序只记一个键。id 非空为 `id:`，否则才用 `name:`，两者都空才用 `addr:`。`mark` / `is` 不再把 id 和 name 放进同一集合做或匹配。
  2. **测试**：`tests/test_postprocessor.cpp:565-624`。A 为 `prog_1` / `WELD_001` 且已 ensure，B 为 `prog_2` / `WELD_001` 且未仿真。`LastTest.log:638-641` 四条 PASS，validate 与 preview 都含 `no fresh simulation`。
  3. **P2 `MAINT-003` RESOLVED**：拒写删除走 `olp::io::utf8ToPath`（`SampleSession.cpp:231-232`），与 `exportToFile` 同一函数。
  4. **本机实跑**：`Tools\Compile.bat` exit 0。`LastTest.log:952` 为 834 passed, 0 failed。`--verify` exit 0。抽核文件均低于 1000 行。`include/olp/algo` 与 `src/algo` 的 `git diff` 为空。
  5. **仍延期**：`MAINT-002`（`ensureProgramSimulated` 恒返回 true）、`FIX-EXPORT-P2-D`（段内近碰撞盖掉奇异文案）。
- **详细审核**：见 `CRDocs/REVIEW.md` 文首。
