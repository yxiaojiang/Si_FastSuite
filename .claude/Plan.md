# Plan.md — 当前活跃事项

> 最近刷新：2026-10-25 夜。完整 28 周排期、模块细则和验收条文仍在根目录 `Plan.md` 与 `Docs/OLP_System_Design_Claude.md`（v1.1）。本文件只保留正在做的事。

1. **[NEXT] 查演示夹具碰撞。** `SiFastSuiteOLP` 退出码为 2：`APPROACH` 与 `WELD_START` 碰到 `Table_Clamp_Block_1`。这是真实干涉。继续用内置平面解析即可，不依赖新库。不要把退出码改成 0。
2. **[DEFERRED] OCCT 装配实体名。** 曲面件已由 OCCT 导入。平面整场景仍走内置解析，因为 XCAF 只给出装配名，认不出 `FANUC_` 与 `Static_Base`。修名字之前不要让 OCCT 抢先解析这些文件。
3. **[BLOCKED] ROBOGUIDE 对拍与阶段 0 外部门禁。** J2/J3 耦合符号、圈数轴、法兰位姿待核。客户实景和 3D PDF 五天熔断未开始。
