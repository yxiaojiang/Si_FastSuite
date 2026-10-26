# SessionBrief.md — 当前接手摘要

> 最近刷新：2026-10-26。主线仍是对标 FASTSUITE Edition 2 的机器人焊接离线编程。审计修复在 `02b8005`。Qt 6.12.0（MSVC 2022 64 位）在 `D:\Soft\Qt\6.12.0\msvc2022_64`。OpenCASCADE 8.0.0 在 `C:\vcpkg\vcpkg\installed\x64-windows`。工作区：`C:\Users\mimovr\Desktop\TEmp\Si_FastSuite`。

---

## 1. 当前目标

用户已确认从主界面开始。分区骨架已经能打开。四件曲面 STEP 已由 OpenCASCADE 导入。演示焊缝起点已沿焊缝收到 32 mm，焊枪离开 `Table_Clamp_Block_1`，角平分线没改。用户决定暂不装 ROBOGUIDE，耦合符号保持 +1 并继续标为待核。完整排期见根目录 `Plan.md` 与 `Docs/OLP_System_Design_Claude.md`（v1.1）。

## 2. 这轮已经落地的事实

产品配置在 `build-full`，生成器是 Visual Studio 17 2022 x64，关闭了 `OLP_HEADLESS`。必须加 `-DVCPKG_MANIFEST_MODE=OFF`，否则仓库里的 `vcpkg.json` 会进入清单模式并试图源码编译 Qt。工具链是 `C:/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake`，`Qt6_DIR` 指向 `D:/Soft/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6`。跑测试时 PATH 要带上 Qt 的 `bin` 和 vcpkg 的 `bin`。

`importStep` 在有 OCCT 时先走 `importStepWithOcct`，失败再退回内置平面解析。没有 OCCT 时仍只用内置解析。管法兰、U 型支架、方管、钢吊架都导入成功，测试确认有非直线边和非平面。OCCT 按 `MANIFOLD_SOLID_BREP` 认每一块实体，实体名为空时单实体文件退回产品名。CAF 传输之后 `HasResult()` 仍是假，但 `ShapeResult()` 能拿到实体。面带上了原点和包围盒，边记下了相邻面。腹板和前箱壁的边集合与内置解析相同，第 3 条焊缝的端点和方向也相同；角平分线偏差小于 0.001 度。方向必须沿曲线参数，不能按面环反向，否则起弧会换到另一头。

运动学顺序是示教器关节角、URDF 耦合、`q = s·J − o`、再进入 OPW。存储偏置不是 ROS-Industrial 那种直接相加。J3 存储偏置是负九十度、符号是负一，对应的 ROS 偏置才是正九十度。J6 折回示教器时要落在负 π 到正 π，否则零圈会被报成负一圈。法兰位置和 `tool0` 的 Z 轴与独立 URDF 链对过，两百组样本误差小于 0.01 毫米。零位法兰大约在 (890, 0, 1200) 毫米，工具轴向是正 X。连杆网格仍是 OPW 抽象，没有逐节对齐 URDF。

J2/J3 耦合写在 URDF 关节空间，系数目前是正一。只动 J2、示教器 J3 保持零时，前臂绝对角应保持不变。圈数暂按 J1、J4、J6 记，发那科 `$TURN_AXIS` 常是 J4、J5、J6，这个对应未核。关节限位按 URDF 几何角检查。耦合后的 J3 用哪套限位也待核。

`build-full` 的 `olp_tests` 五十六项通过，`--verify` 退出码 0。整场景十二个实体。起点收到 32 mm 后，接近点上方有直线点 `CLEARANCE_1`，焊枪不再扫过前箱壁。回退段对这块板的间隙仍低于 5 mm，不挡退出码。直接运行会打开主窗口、加载示例并停在编程工作台。两个工作台都留着场景、属性和关节点动；编程台再显示程序树和「路径点 | 问题」。程序树只到操作，路径点表只含当前操作，问题读路径点缓存。点动读场景机器人，拖动不写入程序。中间是 `OcctViewport`，背景是深灰。因 Qt 6 在 Windows 下忽略 `WA_PaintOnScreen` 并在 `paintEvent`（`SwapBuffers`）后执行 `QBackingStore::flush()`（`GDI BitBlt`）覆盖 `QWidget` 的 `HWND`，`OcctViewport` 在自身带 `WS_CLIPCHILDREN` 的 `HWND` 内建立独立的透传子窗口承载 `WNT_Window`（并开启 `AA_DontCreateNativeWidgetSiblings`），彻底隔离 `QBackingStore` 的 `BitBlt` 覆盖，选中任意非 3D 面板时视口不再变黑。左键旋转，中键或右键平移，滚轮向前拉近、向后拉远。「全景」按可见包围盒取景。工具栏按文件、编辑、工作台、编程、视图分组，图标在上、文字在下。还没接入的命令是灰的。场景画成包围盒。还不能拾取，焊枪拖拽也没挂到画面上。`ShellWindow.h` 不再包含示例会话。文档变更可挂多个监听。`build-full` 不进仓库。

## 3. 未关闭风险

1. 耦合符号、圈数记在哪三个轴、法兰相对示教器的位姿，都没有真机或 ROBOGUIDE 对拍。用户决定先不装 ROBOGUIDE，继续开发，关节程序仍只能当草稿。
2. 回退段对 `Workpiece_BoxWall_Front` 的间隙低于 5 mm，不挡退出码。视口还不能拾取边，焊枪拖拽没挂到画面上。客户实景和 3D PDF 五天熔断都没开始。
3. 算法层不得依赖 Qt。改文档走 `CommandManager`。肩部奇异是几何代理。FANUC 世界原点未对真机。

## 4. 下一步

1. 在现有视口上做边拾取，再把 `TcpGizmo` 画到画面上。不新建选择管理器或点动面板。
2. 真机到位后再核对 J2/J3。3D PDF 五天熔断仍未开始。

## 5. 索引

设计基线：`Docs/OLP_System_Design_Claude.md`、`Docs/FASTSUITE_OLP_System_Architecture.md`。排期：根目录 `Plan.md`。设计复盘：`.claude/contexts/OLP-archive.md`。已做步骤：`.claude/contexts/steps/00-index.md`。视觉沿边是历史归档，见 `.claude/contexts/VisionEdgeSystem.md`。
