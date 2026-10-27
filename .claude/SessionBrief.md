# SessionBrief.md — 当前接手摘要

> 最近刷新：2026-10-27。主线仍是对标 FASTSUITE Edition 2 的机器人焊接离线编程。工作区：`C:\Users\mimovr\Desktop\TEmp\Si_FastSuite`。Qt 6.12.0 在 `D:\Soft\Qt\6.12.0\msvc2022_64`，OpenCASCADE 8.0.0 在 `C:\vcpkg\vcpkg\installed\x64-windows`。

---

## 1. 当前目标

M12 FANUC `.ls` 后处理器完备化 Round 2 复审已通过，任务号 `M12-POST-FANUC-001`，状态 `APPROVED`。`M12-001`、`M12-002`、`M12-003` 及 `M12-005` 正式关闭：速度四舍五入取最近整数，J 轴超出 1..100%、L 轴不足 1 mm/s、CNT 超出 0..100 均硬报错阻断导出，绝不静默改写为 50%、200 或 CNT100；点名超 16 字符与程序注释超 32 字符直接阻断报错，彻底移除静默截断伪通过；关节运动 J 携带 `useWeldSpeed` 非法组合直接阻断报错，输出文本绝不含 `J P[...] WELD_SPEED`。自动化全量测试扩增至 **482 passed, 0 failed**。示例工位 `--verify` 退出码 0，1161 帧，38.5067 秒，成功生成 `WELD_001.ls` 与 HTML 离线预览。各文件均严格低于 1000 行限制。

## 2. 这轮已经落地的事实

1. **FANUC ASCII .ls 结构完备化与前置门禁。** 对标 ROBOGUIDE 与 `MakeTP`，动态精准计算 `/MN` 实际行数写入 `/ATTR` 的 `LINE_COUNT` 并估算 `MEMORY_SIZE`；完备输出 `/APPL` 焊接设备块；前置校验严格拦截无逆解点、未实现圆弧 `C`、超限速度与 CNT、超长注释以及关节焊接速度冲突。
2. **工艺指令与 Home 关节点。** 完整映射 `Arc Start[n]`、`Arc End[n]`、`Weave Sine[n]`、`Weave End`、`WAIT n.nn(sec)`、`DO[n]=ON/OFF` 与自定义行注释 `! comment`；默认尾部追加回 Home 注释行，勾选 `emitHomeAsJointTarget` 时可生成回 Home 关节运动 `J P[home:HOME] 30% FINE` 与 `/POS` 的 GP1 Joint 关节点；多操作连续点编号与批量程序导出 `exportControllerPrograms` 均已闭环。
3. **控制器层与 UI 导出对话框。** `MainWindow` 暴露前置校验、预览、单文件与批量导出接口；新增独立的 `LsExportDialog` 对话框，包含校验状态明细、选项开关、暗色等宽代码预览、剪贴板复制及另存为文件对话框；主工具栏与文件菜单的“导出”动作全面激活。
4. **单文件行数强制合规。** `ShellWindow.cpp` 为 880 行，`FanucLsPostProcessor.cpp` 为 283 行，`LsExportDialog.cpp` 为 188 行，`test_postprocessor.cpp` 为 245 行，所有源文件行数均受控在 900 行以下。

## 3. 未关闭风险

1. J2/J3 耦合符号、圈数记在哪三轴、法兰相对示教器的位姿，仍无真机或 ROBOGUIDE 对拍（`RISK-001`）。关节程序仍只能当几何草稿。
2. 回退段对前侧板间隙低于 5 mm，不挡退出码（`RISK-002`）。
3. 客户实景和 3D PDF 五天熔断机制未正式开始（`RISK-003`）。
4. `M12-004`（导出对话框警告显示与多段程序选择联动）与 `M12-006`（`.ls` 头部日期/文件名示教器严格样本对拍）在 `CRDocs/DEFERRED.md`。
5. 插补延期项 `M11-002` ~ `M11-006` 留在 `DEFERRED.md`。

## 4. 下一步

1. 推进 M13：工位交付与导出系统（基于 Qt `QPdfWriter` 的 2D PDF 工位报告导出、单文件 HTML 3D 离线轻量交互包优化、3D PDF 预研与五天熔断机制评估）。
2. 不要改动 `CRDocs/REVIEW.md` 里已通过的 M10、M11 与 M12 结论，延期项在进入对应模块前不顺手打开。

## 5. 索引

设计基线：`Docs/OLP_System_Design_Claude.md`、`Docs/FASTSUITE_OLP_System_Architecture.md`。排期：根目录 `Plan.md`。交接：`CRDocs/`。设计复盘：`.claude/contexts/OLP-archive.md`。已做步骤：`.claude/contexts/steps/00-index.md`。
