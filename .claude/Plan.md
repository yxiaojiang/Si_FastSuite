# Plan.md — 当前活跃事项

> 最近刷新：2026-10-25 晚。完整 28 周排期、模块细则和验收条文仍在根目录 `Plan.md` 与 `Docs/OLP_System_Design_Claude.md`（v1.1）。本文件只保留正在做的事。

1. **[NEXT] 接通 OpenCASCADE 导入。** OCCT 8.0.0 已装在 `C:\vcpkg\vcpkg\installed\x64-windows`。产品配置还缺 Qt 6（MSVC 2022 64 位）。装好后用 `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake` 做非 headless 编译，修 `importStepWithOcct`，并用管法兰、U 型支架、方管、钢吊架验收边长和法向。
2. **[IN PROGRESS] 演示路径与夹具碰撞。** `SiFastSuiteOLP` 在 headless 下退出码为 2：`APPROACH` 与 `WELD_START` 碰到 `Table_Clamp_Block_1`。这是真实干涉，不依赖 Qt 也能查。
3. **[BLOCKED] ROBOGUIDE 对拍与阶段 0 外部门禁。** J2/J3 耦合符号、圈数轴、法兰位姿待核。客户实景和 3D PDF 五天熔断未开始。
