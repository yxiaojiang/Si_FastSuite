# M13-REPORT-PDF-001 — Round 8：闭环满量程姿态 `-180.0, -180.0, -180.0` 宽度与构型列无重叠分界

> 执行者：Executor
>
> 日期：2026-10-27
>
> 轮次：Round 8
>
> 状态：APPROVED（Round 8 复审已通过，见 `STATUS.md` 与 `REVIEW.md`）

本轮针对 Round 7 `REVIEW.md` 中唯一剩余项（满量程姿态 `-180.0, -180.0, -180.0` 实测宽度 `726 px` 大于姿态格 `680 px`），在不缩小已通过的坐标列（`850 px`）、状态列（`413 px`）和焊缝摘要文本框（`516 px`）的前提下，从点位表第 2 子行的构型格及两格间隙调配宽度，使姿态格与构型格、事件格严格分离不重叠且全部容纳各自满量程边界文本，并在同一台 300 DPI `QPdfWriter` 上新增对应单元测试断言。未修改 `REVIEW.md` 或 `DEFERRED.md`。

---

## 本轮实际修改

- `include/olp/ui/WorkcellPdfReportExporter.h`（88 行）
  - 新增点位表第 2 子行布局常量并暴露共用姿态格式化函数 `formatTargetOrientation(double wDeg, double pDeg, double rDeg)`：
    - 姿态列（WPR）：`kTargetWprColumnStart = 12`，`kTargetWprColumnWidth = 730`（区间 `12..742`，容纳 `-180.0, -180.0, -180.0` 实测 `726 px <= 730 px`）；
    - 构型列（CONFIG）：`kTargetConfigColumnStart = 746`，`kTargetConfigColumnWidth = 580`（区间 `746..1326`，与姿态格相距 `4 px` 不重叠，容纳 `F D B, -1, -1, -1` 实测 `561 px <= 580 px`）；
    - 工艺事件列（Events）：`kTargetEventColumnStart = 1330`，宽 `W - 1340 = 903 px`（区间 `1330..2233`，与构型格相距 `4 px` 不重叠，保持 `903 px` 不变）。
  - 第 1 子行坐标列 `kTargetXyzColumnWidth = 850`（`980..1830`）、状态列 `kTargetStatusColumnStart = 1830`（宽 `413 px`）及总览焊缝摘要（文本框 `516 px`）保持不变。
- `src/ui/WorkcellPdfReportExporter.cpp`（705 行）
  - 实现 `WorkcellPdfReportExporter::formatTargetOrientation`，保留一位小数输出 `wDeg, pDeg, rDeg`；
  - 点位表表头与数据行的第 2 子行统一使用 `kTargetWprColumnStart` / `kTargetWprColumnWidth`、`kTargetConfigColumnStart` / `kTargetConfigColumnWidth`、`kTargetEventColumnStart` 与 `formatTargetOrientation` 绘制。
- `tests/test_report.cpp`（680 行）
  - 在 300 DPI `QPdfWriter` 上直接调用 `WorkcellPdfReportExporter::formatTargetOrientation(-180.0, -180.0, -180.0)`，断言：
    1. `fm8.horizontalAdvance("-180.0, -180.0, -180.0") <= kTargetWprColumnWidth`（`726 <= 730`）；
    2. `kTargetWprColumnStart + kTargetWprColumnWidth < kTargetConfigColumnStart`（`742 < 746`，姿态格与构型格分离不重叠）；
    3. `kTargetConfigColumnStart + kTargetConfigColumnWidth < kTargetEventColumnStart`（`1326 < 1330`，构型格与事件格分离不重叠）；
    4. `F D B, -1, -1, -1`（`561 px`）、`N U T, 0, 0, 0`（`462 px`）与 `构型 CONFIG` 表头（`297 px`）均 `<= kTargetConfigColumnWidth`（`580 px`）。

---

## 验证证据

```powershell
cmake --build build-full --config Release
$env:PATH = "D:\Soft\Qt\6.12.0\msvc2022_64\bin;C:\vcpkg\vcpkg\installed\x64-windows\bin;" + $env:PATH
.\build-full\Release\olp_tests.exe
.\build-full\Release\SiFastSuiteOLP.exe --verify
```

- 全量自动化测试：**606 passed, 0 failed**（退出码 0）。
- Test 20 关键输出：
  ```
  [PASS] M13-001: standard WPR value fits rebalanced WPR column
  [PASS] M13-001: 'N U T, 0, 0, 0' advance fits in config sub-row rect (580px)
  [PASS] M13-001: WPR header fits rebalanced WPR column
  [PASS] M13-001: config header fits rebalanced config column
  [PASS] M13-001: maximum config value fits rebalanced config column
  [PASS] M13-001: fractional four-digit XYZ fits Cartesian column
  [PASS] M13-001: supported negative XYZ boundary fits Cartesian column
  [PASS] M13-001: WPR and config columns are separated and do not overlap
  [PASS] M13-001: config and event columns are separated and do not overlap
  [PASS] M13-001: full-angle WPR boundary (-180.0, -180.0, -180.0) fits rebalanced WPR column
  [PASS] M13-001: supported long seam summary fits overview cell
  ```
- 端到端 `--verify`：退出码 0，生成 1161 帧，周期 38.5067 s（焊接 33.305 s，空走 5.20174 s）。
- 文件行数检查：`include/olp/ui/WorkcellPdfReportExporter.h` 88 行，`src/ui/WorkcellPdfReportExporter.cpp` 705 行，`tests/test_report.cpp` 680 行，`src/ui/ShellWindow.cpp` 980 行，全部低于 1000 行上限。

---

# M13-REPORT-PDF-001 — Round 7：按边界矩阵重整目标行布局

> 执行者：Executor
>
> 日期：2026-10-27
>
> 轮次：Round 7
>
> 状态：READY_FOR_REVIEW

Round 6 的逐字符串压缩无法覆盖真实动态值；本轮改为一次性布局契约：XYZ 列支持一位小数、带符号的 `±9999.9 mm`，状态列缩至仍覆盖全部当前状态文案的宽度，焊缝摘要由绘制与测试共用同一格式化函数。此处是目标行的整体重新分配，不把 Round 6 的“已通过矩形不得缩小”误用为禁止为坐标数据让位。

## 本轮实际修改

- `include/olp/ui/WorkcellPdfReportExporter.h`
  - 定义目标行 XYZ 宽度 `850 px` 与状态列起点 `1830 px`，并暴露绘制与测试共用的 `formatTargetPosition`、`formatSeamSummary`。
- `src/ui/WorkcellPdfReportExporter.cpp`
  - XYZ 从 `980..1640` 的 `660 px` 改为 `980..1830` 的 `850 px`；状态列为余下 `413 px`。
  - 坐标统一用一位小数显示，不再用仅对 `.0` 有效的局部压缩逻辑。
  - 总览焊缝值改为紧凑且无信息丢失的 `数量/长度mm`；单元格标签仍给出焊缝及规划长度的语义。
- `tests/test_report.cpp`
  - 直接调用实际格式化函数，覆盖一位小数四位坐标、三个负的 `-9999.9 mm` 坐标、`999/99999.9mm` 焊缝摘要，以及 10 个状态字符串。
