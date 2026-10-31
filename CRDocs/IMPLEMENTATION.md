# Implementation Report — Task MAINT-CLEANUP-P2-001 (Round 2)

- **Task ID**: `MAINT-CLEANUP-P2-001`
- **Round**: `2`
- **Status**: `READY_FOR_REVIEW`
- **Executor**: Executor
- **Date**: 2026-10-31

---

## 1. Round 1 审核意见整改闭环

在 Round 1 独立代码审核中，Reviewer 给出了 `CHANGES_REQUESTED` 结论，指出 1 项 P1 阻塞问题及延期清单梳理要求：
1. **`MAINT-001 [OPEN] 仿真记账把不同程序当成同一段（P1）`**：
   - **根本原因**：Round 1 中 `markProgramEnsured` 将 `id`、`name`、`prog_addr_<ptr>` 分别作为多个独立条目插入同一 `exportSimEnsuredPrograms_` 集合，且 `isProgramEnsured` 进行或（OR）匹配。在多程序工程中，若各程序使用默认名称 `WELD_001` 且具有不同 `id`（如 `prog_1` 与 `prog_2`），当 `prog_1` 执行仿真后，集合中写入了 `WELD_001`；此时对未仿真的 `prog_2` 调用 `validateProgramForExport("prog_2")` 时，因其名称 `WELD_001` 命中而误判为已仿真，导致 const 校验/预览门禁被击穿。
   - **整改方案**：
     - 在 `src/ui/MainWindow.cpp` 引入 `programCanonicalKey(const core::Program& prog)` 单一规范键生成机制：
       ```cpp
       static std::string programCanonicalKey(const core::Program& prog) {
           if (!prog.id.empty()) {
               return "id:" + prog.id;
           }
           if (!prog.name.empty()) {
               return "name:" + prog.name;
           }
           return "addr:" + std::to_string(reinterpret_cast<uintptr_t>(&prog));
       }
       ```
     - 一段程序在集合中**严格仅对应一个带前缀的独立键**。`id` 非空时，强锁定使用 `"id:" + prog.id` 记账，彻底阻断利用 `name` 进行二次命中的可能；同时前缀机制杜绝了不同程序之间 `id` 与 `name` 字符串相同导致的误碰。
     - `markProgramEnsured` 与 `isProgramEnsured` 均严格通过 `programCanonicalKey` 单点记账与判别。
   - **单测证据**：
     - 在 `tests/test_postprocessor.cpp` 中新增 `MAINT-001` 专项回归测试：程序 A 为 `id=prog_1, name=WELD_001` 且已 ensure，程序 B 为 `id=prog_2, name=WELD_001` 且未仿真；断言 `validateProgramForExport("prog_2")` 与 `previewFanucLs("prog_2")` 均被正确拦截并包含 `"no fresh simulation"` 报错信息。

2. **`MAINT-003 [RESOLVED in Round 2] 拒写时删除旧 .ls 接入 utf8ToPath`**：
   - 在 `src/ui/SampleSession.cpp` 引入 `#include "olp/io/Utf8Path.h"`。
   - 拒写分支使用 `std::filesystem::remove(olp::io::utf8ToPath(lsOut), ec)`，与正式写出 `FanucLsPostProcessor::exportToFile` 保持完全一致的跨平台 UTF-8 路径映射。

3. **延期清单规整**：
   - `CRDocs/DEFERRED.md` 明确将 `FIX-EXPORT-P2` 调整为 `P2-A/B/C` 收口（`CLOSED`）；
   - `FIX-EXPORT-P2-D`（段内近碰撞覆盖奇异说明）与 `MAINT-002`（`ensureProgramSimulated` 恒返回 true）保持在 `CRDocs/DEFERRED.md` 中继续处于 `OPEN` 状态。

---

## 2. 修改文件与行数门禁清单

| 文件路径 | 状态 | 修改后行数 | 门禁合规性（< 1000 行软上限，1500 硬上限） |
|---|---|---|---|
| `src/app/CommandManager.cpp` | 精简拆分 | **477** | **OK**（稳定在 400~600 行黄金区间） |
| `src/app/ToolpathCommands.cpp` | 拆分新编译单元 | **528** | **OK**（稳定在 400~600 行黄金区间） |
| `include/olp/app/CommandManager.h` | 保持兼容 | **336** | **OK**（远低于 1000 行） |
| `include/olp/ui/MainWindow.h` | 接口与状态更新 | **166** | **OK**（远低于 1000 行） |
| `src/ui/MainWindow.cpp` | `programCanonicalKey` 记账 | **790** | **OK**（低于 1000 行软上限） |
| `src/ui/SampleSession.cpp` | `utf8ToPath` 接入 | **273** | **OK**（远低于 1000 行） |
| `tests/test_postprocessor.cpp` | MAINT-001 专项断言 | **693** | **OK**（远低于 1000 行） |
| `CMakeLists.txt` | 构建配置更新 | **224** | **OK** |
| `CRDocs/DEFERRED.md` | 清单梳理与收口 | **446** | **OK** |

---

## 3. 实跑验证证据

Windows MSVC 2022（Release 模式）运行 `Tools\Compile.bat`：
- **编译结果**：0 警告，0 错误，exit code 0。
- **CTest 套件**：
  ```
  Test project C:/Users/mimovr/Desktop/TEmp/Si_FastSuite/build
      Start 1: SiFastSuiteOLP_AllTests
  1/1 Test #1: SiFastSuiteOLP_AllTests ..........   Passed    4.18 sec
  100% tests passed, 0 tests failed out of 1
  ```
- **测试断言明细**（`build/Testing/Temporary/LastTest.log`）：
  ```
  Summary: 834 passed, 0 failed.
  Test Passed.
  ```
  （原 830 passed，新增 4 项 MAINT-001 断言全部为 PASS）。
- **SiFastSuiteOLP.exe --verify 离线验证**：
  - 成功导入工位并替换 M-10iA 机器人；
  - 焊缝拾取与 9 个采样帧生成；
  - OPW 逆解、碰撞检查与 1047 帧烘焙完成；
  - 成功写出 `build/WELD_001.ls` 与 `build/WELD_001_Preview.html`；
  - 进程 exit code 0。

---

## 4. 架构与边界审查

1. **OpwKinematics 与 olp_algo 保持零改动**：
   - `git diff include/olp/algo/ src/algo/` 结果为空。
   - `olp_algo` 保持纯算法层，零 Qt 依赖。
2. **规程合规**：
   - 未重开已 CLOSED 的 `PlanDocs/FIX-EXPORT-SAFETY-PLAN.md`。
   - P1 阻塞项全闭环，P2 延期项精准记录。
