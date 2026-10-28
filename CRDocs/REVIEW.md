# 审核结论 — M15-TJOINT-REGRESSION-001 Round 1

> **Task ID**：`M15-TJOINT-REGRESSION-001`
> **Task**：结束 3D PDF 悬置（收口 RISK-003）并建立 SampleScenes T 型接头闭环自动化回归测试（DIR-001 第 7 步，M13/M15）
> **Round**：1
> **Reviewer**：审核方
> **Reviewed at**：2026-10-28
> **Executor**：执行方
> **Verdict**：**APPROVED**
>
> 审核方读了 `tests/test_tjoint_regression.cpp`、`buildSeamFromPickedEdge`、`computeSeamFrame`、`extendStartTrimUntilTorchClears`、`insertSafeTransitBeforeApproach`，以及 `validateAndSimulateProgram` 里点状态的赋值。复跑时读到的中间量：立板法向 `(0, -1, 0)`，底板法向 `(0, 0, 1)`，平分线与 `(0, -1, 1)` 的有符号点积是 1。采样 9 帧，弧长从 5 mm 到 395 mm，严格递增。起弧切边停在 5 mm。12 个目标点状态都是 `Valid`，最小间隙 126.179 mm，问题 0 条。Home 到切入点插入了 1 个 `CLEARANCE_1`，间隙 281.436 mm。左右夹具的碰撞角色是 `Fixture`。`build-full/Release/olp_tests.exe` 由审核方用 `/p:VCToolsVersion=14.44.35207` 重编后复跑：**752 passed, 0 failed**，退出码 0。`SiFastSuiteOLP.exe --verify` 退出码 0，1047 帧，34.7508 秒。当前范围没有新的 P0 或 P1。`M15-001` 记入延期。

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M15-001 | P2 | **DEFERRED** | `tests/test_tjoint_regression.cpp` | 平分线用了绝对值。碰撞、近碰撞、奇异和近限位不会让这条回归失败。夹具角色和弧长单调没有断言。见 `DEFERRED.md`。 |

`RISK-003` 按这段定调用 **CLOSED** 收口。`RISK-001`、`RISK-002` 仍开着。第 7 步没有改耦合比，也没有改播放、工程 JSON 和 2D PDF 的产品代码。

### M15-001 [P2] DEFERRED

平分线偏差用的是 `abs(dot)`。反向平分线的偏差也是 0°。两个外法向没有单独比较。点位循环只把不可达、超限位和构型跳变算作失败；`cache.valid` 在碰撞时仍是 true。夹具只对了名字。弧长只比较了首尾差。

这一轮读到的数是对的：有符号点积为 1，12 个点都是 `Valid`，弧长严格递增，两个夹具都是 `Fixture`。这些缺口不挡本次通过。

---

## 本轮已核对并保持

- `RISK-003` 的收口段写在 `CRDocs/DEFERRED.md`、`.claude/SessionBrief.md`、`.claude/Plan.md` 和根目录 `Plan.md`。内容与设计基线的熔断出口一致：停止自研 U3D/PRC 链路，三维交互交付收口到单文件离线 HTML，2D 报告继续用 QPdfWriter，客户另有要求时再评估商业 SDK。HTML 动画在里程碑表里仍是「未做」。设计基线正文没有改，与 `DIR-001`「不另写 Docs/ 基线」一致。
- 导入得到 4 个实体。底板和立板是 `Workpiece`，左右夹具是 `Fixture`。立板 Edge 3 长 400 mm。跨零件侧面是 `part_2` 与 `part_1`。焊缝跨度 390 mm，对应两端各 5 mm 切边。
- 焊丝尖端在焊接角色下不报碰撞，关掉豁免后报接触。枪放到左夹具盒内时报 `Fixture_Clamp_Left`。
- 这条前边在初始 5 mm 切边下已经离开夹具，`extendStartTrimUntilTorchClears` 没有再加大切边。交接里写的「检测到夹具后自适应内缩」在这条边上没有发生。Home 到切入点的关节扫掠被挡住后，插入了 1 个 `CLEARANCE_1`。随后整段程序没有碰撞问题。
- 烘焙 1507 帧。节拍 49.8037 秒，其中焊接 39.0414 秒，空走 10.7623 秒。`.ls` 含 `/PROG TJOINT_001`、`Arc Start[1]`、`Arc End[1]` 和 `/POS`，并写到 `build/TJOINT_001.ls`。
- 新测试只核对了 `a1`、`c1`、`c2`、`c3`。模型里的 `a2 = -150`、`c4 = 100` 仍由既有运动学金样覆盖。`RISK-001` 不关。
- `tests/test_tjoint_regression.cpp` 299 行，`tests/test_main.cpp` 67 行，`tests/test_support.h` 34 行，`CMakeLists.txt` 221 行。都在 1000 行以内。
- `--verify` 仍打印接近段和回退段对 `Workpiece_BoxWall_Front` 的间隙告警。`RISK-002` 保持打开。

---

# 审核结论 — M14-PROJECT-JSON-001 Round 3

> **Task ID**：`M14-PROJECT-JSON-001`
> **Task**：工程保存先做 JSON（DIR-001 第 6 步，M14）
> **Round**：3
> **Reviewer**：审核方
> **Reviewed at**：2026-10-28
> **Executor**：执行方
> **Verdict**：**APPROVED**
>
> 审核方读了 `saveToFile` 的替换失败分支。`src` 里已经没有 `copy_file`。`build-full/Release/olp_tests.exe` 的写入时间是 19:35:20，晚于 `ProjectSerializer.cpp`（19:34:35）和 `tests/test_project_io.cpp`（19:34:47）。审核方复跑：**725 passed, 0 failed**，退出码 0。`SiFastSuiteOLP.exe --verify` 退出码 0，1047 帧，34.7508 秒。`M14-009` 关闭。

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M14-009 | P1 | **RESOLVED** | `src/io/ProjectSerializer.cpp`<br>`tests/test_project_io.cpp` | `MoveFileExW` 失败时返回 false，并删掉临时文件。目标文件只在替换成功时换成新内容。 |
| M14-008 | P1 | **RESOLVED** | `src/io/ProjectSerializer.cpp` | Round 2 已关闭，本轮没有改这条路径。 |
| M14-010 | P1 | **RESOLVED** | `src/io/ProjectSerializer.cpp` | Round 2 已关闭。中文路径那条仍为 PASS。 |
| M14-011 | P2 | **RESOLVED** | `tests/test_project_io.cpp` | Round 2 已关闭。 |
| M14-012 | P2 | **DEFERRED** | `ShellWindow::openProjectFile` | 打开前仍不询问。见 `DEFERRED.md`。 |
| M14-013 | P2 | **DEFERRED** | `MainWindow::openProject` | 打开仍不重建 CAD 形体。见 `DEFERRED.md`。 |

`RISK-001` 仍开着。第 7 步没有改。当前范围内的 P1 都已关闭。

### M14-009 [P1] RESOLVED

正文仍先写到 `目标路径.tmp`。`flush` 或 `close` 失败时删掉临时文件并返回 false，此时还没有调用 `MoveFileExW`。

替换只有 `MoveFileExW`，标志是 `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`。失败时先记下 `GetLastError()`，再删临时文件，然后返回 false。没有把临时文件拷到目标路径上。`saveProject` 只在返回 true 时把 `isDirty` 写成 false。

覆盖测试先写入 `old_corruptible_content_prior_to_save`，保存后再读回，场景节点数是 3。这条为 PASS。失败测试把目标设成 `temps` 目录：保存返回 false，`temps.tmp` 不存在。后面的中文路径仍写在这个目录下并通过，目录没有被换成工程文件。

---

## 本轮已核对并保持

- `fileFormat` 和 `schemaVersion` 的拒绝逻辑还在。`{}` 不替换调用方文档。
- 中文路径仍走 `utf8ToPath`。`M14-010` 三条为 PASS。
- `ProjectSerializer.cpp` 898 行，`tests/test_project_io.cpp` 582 行，`MainWindow.cpp` 599 行，`ShellWindow.cpp` 863 行。都在 1000 行以内。
- `OpwKinematics` 和 3D PDF 不在本轮修改里。`M14-012`、`M14-013` 仍延期。

---

# 审核结论 — M14-PROJECT-JSON-001 Round 2

> **Task ID**：`M14-PROJECT-JSON-001`
> **Task**：工程保存先做 JSON（DIR-001 第 6 步，M14）
> **Round**：2
> **Reviewer**：审核方
> **Reviewed at**：2026-10-28
> **Executor**：执行方
> **Verdict**：**CHANGES_REQUESTED**
>
> 审核方读了 `fromJsonString`、`saveToFile`、`utf8ToPath` 和 `MainWindow::saveProject`。`build-full/Release/olp_tests.exe` 的写入时间是 17:14:36，晚于 `ProjectSerializer.cpp`（17:14:02）和 `tests/test_project_io.cpp`（17:13:47）。审核方复跑：**723 passed, 0 failed**，退出码 0。`SiFastSuiteOLP.exe --verify` 退出码 0，1047 帧，34.7508 秒。本机 ANSI 代码页仍是 936。`M14-008`、`M14-010`、`M14-011` 关闭。`M14-009` 仍开着。

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M14-008 | P1 | **RESOLVED** | `src/io/ProjectSerializer.cpp`<br>`tests/test_project_io.cpp` | 缺 `fileFormat`、格式名不对、版本过高、`fileFormat` 为数字、`schemaVersion` 为字符串，都返回 false。`{}` 不改调用方文档。异常留在 `fromJsonString` 里。 |
| M14-009 | P1 | **OPEN** | `src/io/ProjectSerializer.cpp` | 正文先写入 `目标路径.tmp`，成功路径用 `MoveFileExW` 替换。替换失败后的拷贝回退会直接覆盖原文件，并且拷贝失败仍可能返回 true。 |
| M14-010 | P1 | **RESOLVED** | `src/io/ProjectSerializer.cpp`<br>`tests/test_project_io.cpp` | 路径按 UTF-8 转成宽字符再打开。中文目录 `工位测试_工程保存_001/工位工程.json` 的保存、读回和 `userModified` 均为 PASS。 |
| M14-011 | P2 | **RESOLVED** | `tests/test_project_io.cpp` | 采样帧比较了切向、两个面法向、角平分线和 `frame`。零件网格比较了顶点数、三角形数和包围盒。 |
| M14-012 | P2 | **OPEN** | `src/ui/MainWindow.cpp` | 保存成功后 `isDirty` 已清掉。打开另一份工程仍然不询问。见 `DEFERRED.md`。 |
| M14-013 | P2 | **DEFERRED** | `src/ui/MainWindow.cpp`<br>`src/ui/OcctViewport.cpp` | 打开仍不重建 CAD 形体。见 `DEFERRED.md`。 |

`RISK-001` 仍开着。第 7 步没有改。

### M14-008 [P1] RESOLVED

`fileFormat` 必须是字符串，并且等于 `SiFastSuite_Project`。`schemaVersion` 必须是整数，大于 1 就拒绝。`json::parse` 和后面的 `get` 都在同一个 `try` 里。失败时函数返回 false，`outDoc = std::move(loaded)` 还没执行，调用方的文档保持原样。

Test 22 对 `{}`、整数 `fileFormat`、字符串 `schemaVersion` 均为 PASS。`{}` 之后 `lengthUnit` 仍是 `canary_unit`。

### M14-009 [P1] OPEN

`toJsonString` 在打开任何文件之前完成。临时文件是 `目标路径.tmp`，用 `trunc` 打开的是这份临时文件。`flush` 和 `close` 失败时会删掉临时文件并返回 false，目标文件还没被打开。覆盖一份内容为 `old_corruptible_content_prior_to_save` 的文件之后，读回的是新工程。这一条为 PASS。

`MoveFileExW` 失败时进入拷贝回退：

```850:858:src/io/ProjectSerializer.cpp
    if (!MoveFileExW(tempPath.c_str(), targetPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        std::filesystem::copy_file(tempPath, targetPath, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tempPath, ec);
        if (ec) {
            setError("替换目标工程文件失败: " + filePath + " (" + ec.message() + ")");
            return false;
        }
    }
```

`copy_file` 带着 `overwrite_existing` 直接写目标文件。拷贝写到一半失败时，上一份工程已经被改掉。随后的 `remove` 把 `ec` 换成删除临时文件的结果。临时文件删掉了、拷贝却失败时，`ec` 是成功，函数返回 true。`MainWindow::saveProject` 看到 true 就把 `isDirty` 清掉。界面显示保存成功，新内容不在磁盘上。

替换失败时删掉临时文件并返回 false。不要把临时文件拷到目标路径上。目标文件在 `MoveFileExW` 成功之前保持原字节。

### M14-010 [P1] RESOLVED

`utf8ToPath` 用 `MultiByteToWideChar(CP_UTF8)` 得到 `wstring`，再交给 `std::filesystem::path`。`ofstream` / `ifstream` 打开的是这个宽路径。测试文件名含「工位测试」和「工位工程」。保存、读回、`userModified == true` 均为 PASS。

### M14-011 [P2] RESOLVED

M14-004 对五帧的 `tangentX`、`normalFace1`、`normalFace2`、`bisector` 和 `frame` 做了 \(10^{-9}\) 比较，结果为 PASS。M14-006 的零件带 3 个顶点和 1 个三角形。读回的顶点数、三角形数和 `bboxMax` 均为 PASS。

### M14-012 [P2] OPEN

`saveProject` 在 `saveToFile` 返回 true 之后把 `doc_.isDirty` 写成 false。失败时不改这个标志。打开工程的路径没有询问。剩余部分仍在 `DEFERRED.md`。

---

## 本轮已核对并保持

- 位姿仍写 `rot` 矩阵。读入时有矩阵就用矩阵。万向锁那条仍在 \(10^{-9}\) 以内。
- `userModified`、示教偏移、构型圈数、起弧、摆焊和收弧仍在 Test 22 里通过。
- `ProjectSerializer.cpp` 902 行，`tests/test_project_io.cpp` 574 行，`MainWindow.cpp` 599 行，`ShellWindow.cpp` 仍是 863 行。都在 1000 行以内。
- `OpwKinematics` 和 3D PDF 不在本轮修改里。`RISK-001` 仍开着。`M14-013` 仍延期。

---

# 审核结论 — M14-PROJECT-JSON-001 Round 1

> **Task ID**：`M14-PROJECT-JSON-001`
> **Task**：工程保存先做 JSON（DIR-001 第 6 步，M14）
> **Round**：1
> **Reviewer**：审核方
> **Reviewed at**：2026-10-28
> **Executor**：执行方
> **Verdict**：**CHANGES_REQUESTED**
>
> 审核方读了 `ProjectSerializer` 的读写、`MainWindow::saveProject` / `openProject` 和 `ShellWindow` 的文件对话框，并对照 `ProjectModel.h` 的字段。`build-full/Release/olp_tests.exe` 的写入时间是 16:39:30，晚于 `ProjectSerializer.cpp`（16:37:54）和 `tests/test_project_io.cpp`（16:39:06）。审核方复跑：**710 passed, 0 failed**，退出码 0。`SiFastSuiteOLP.exe --verify` 退出码 0，1047 帧，34.7508 秒。本机 ANSI 代码页是 936。用 UTF-8 字节调用 `CreateFileA` 打开带「工程」的路径，返回错误 3，文件没有创建；`CreateFileW` 能创建同一路径。

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M14-008 | P1 | **OPEN** | `src/io/ProjectSerializer.cpp` | 缺 `fileFormat` 的 JSON 会当成空工程打开。字段类型不对时异常直接冒出。 |
| M14-009 | P1 | **OPEN** | `src/io/ProjectSerializer.cpp` | 保存先把目标文件截断，再生成正文。写入中断时上一份工程已经没了。 |
| M14-010 | P1 | **OPEN** | `src/io/ProjectSerializer.cpp`<br>`src/ui/ShellWindow.cpp` | 对话框给出的 UTF-8 路径用窄字符打开。代码页 936 下，中文路径保存失败。 |
| M14-011 | P2 | **DEFERRED** | `tests/test_project_io.cpp` | 采样帧的切向、面法向、局部坐标系和零件网格没有断言。见 `DEFERRED.md`。 |
| M14-012 | P2 | **DEFERRED** | `src/ui/MainWindow.cpp` | 保存成功后 `isDirty` 仍为 true。打开不提示未保存修改。见 `DEFERRED.md`。 |
| M14-013 | P2 | **DEFERRED** | `src/ui/MainWindow.cpp`<br>`src/ui/OcctViewport.cpp` | `cadHandleId` 是本次进程的编号。打开不按 `sourceCadFile` 重新导入。见 `DEFERRED.md`。 |

`RISK-001` 仍开着。第 7 步没有改。

### M14-008 [P1] OPEN

`fromJsonString` 只在 `fileFormat` 这个键存在时才比较字符串。键不存在就继续装入。`{}` 返回 true，场景、焊缝、控制器都是空的，构造函数放进文档的那条工艺条件也被清掉。菜单打开这种文件时，状态栏写「工程已打开」，当前工位被换掉。

`json::parse` 的抛异常被关掉了。后面的 `get` 和 `value` 没有包在 `try` 里。`fileFormat` 若是数字，`schemaVersion` 若是字符串，关节角、圈数或网格下标的类型对不上，nlohmann 抛 `type_error`。异常从打开槽函数冒出去，进程终止，还没保存的工位一起没了。Test 22 的 M14-007 只拒绝了语法坏掉的文本、错误的格式名和过高的版本号。

`fileFormat` 必须存在，并且等于 `SiFastSuite_Project`，否则返回 false，调用方传入的文档保持原样。解析和类型错误都返回 false，并写入 `errorOut`，不要让异常离开 `fromJsonString` 和 `loadFromFile`。补两条断言：`{}` 被拒绝，且传入的文档不变；`fileFormat` 为数字时返回 false，不抛异常。

### M14-009 [P1] OPEN

`saveToFile` 用 `std::ios::trunc` 打开目标，然后才调用 `toJsonString`。文件一打开，原来的工程已经被截成空文件。正文生成或写入中途失败，上一份能打开的工程就没了。`ofs << content` 之后只看了 `good()`。数据还在缓冲区里，要到流关闭才落盘；关闭失败时函数已经返回 true。

先在内存里生成全文，写到同目录的临时文件，`flush` 并关闭成功后，再替换目标文件。替换之前失败，原文件保持原字节。下一轮会看 `saveToFile`：调用 `toJsonString` 之前没有用 `trunc` 打开最终路径。覆盖保存之后读回的是新内容。

### M14-010 [P1] OPEN

`ShellWindow::saveProjectFile` 和 `openProjectFile` 把 `QFileDialog` 的路径用 `QString::toStdString()` 交下去。Qt 6 这里得到的是 UTF-8。`saveToFile` / `loadFromFile` 用 `std::ofstream` / `std::ifstream` 的窄字符重载打开。本机 ANSI 代码页是 936。审核方把 `temps\工程_probe\a.json` 的 UTF-8 字节交给 `CreateFileA`，返回错误 3，文件不存在；同一个路径交给 `CreateFileW` 能创建。中文目录下的保存会失败，或写到按 GBK 解释出来的另一个名字。M14-006 写的是纯 ASCII 的 `temps` 路径，所以是绿的。

在 Windows 上把这个 `std::string` 按 UTF-8 解释，用宽路径打开。补一条路径里含非 ASCII 字符的保存再读回，`userModified` 与打开前一致。

---

## 本轮已核对并保持

- 位姿写的是 `rot` 矩阵加平移。读入时有 `rot` 就用矩阵，`wpr` 只在没有矩阵时才用。万向锁 \(P = +90^\circ\) 的往返误差小于 \(10^{-9}\)。nlohmann 对 IEEE double 使用 Grisu2，可以按 `strtod` 还原。
- `userModified`、`processModified`、`userDeltaOffset`、`lockConfig`、构型的 flip / up / turns、`ArcStart`、`WeaveStart`、`ArcEnd` 都写进了 JSON。Test 22 这几条为 PASS。
- 焊缝的拓扑签名、起收弧长度、采样点位置和角平分线为 PASS。切向、两个面法向和局部坐标系也写进了文件，断言没有覆盖，记在 `M14-011`。
- 单位设置和工艺条件的电流、电压、气体、`weldSpeedCmPerMin`、`weldSpeedMmPerSec` 都按字段写回。走 `setWeldSpeedCmPerMin(60)` 的那条，读回是 60 cm/min 和 10 mm/s。
- 被接住的打开失败发生在替换文档之前，原文档还在。成功打开会换上新文档，清空命令栈和选中项，再校验第一段操作。这次校验写的是 `cache`，不改 `poseInUFrame`、`userModified` 和工艺事件。
- `ProjectSerializer.cpp` 789 行，`tests/test_project_io.cpp` 501 行，`MainWindow.cpp` 595 行，`ShellWindow.cpp` 863 行，都在 1000 行以内。
- `OpwKinematics` 和 3D PDF 不在本轮 diff 里。`RISK-001` 仍开着。

---

# 审核结论 — M11-PLAYBACK-SHAPE-001 Round 2

> **Task ID**：`M11-PLAYBACK-SHAPE-001`
> **Task**：播放时用 URDF 连杆链驱动外形（DIR-001 第 5 步，M11-005）
> **Round**：2
> **Reviewer**：审核方
> **Reviewed at**：2026-10-28
> **Executor**：执行方
> **Verdict**：**APPROVED**
>
> 审核方读了 `sampleTrajectory`、`syncScene`、`stopPlayback` 和 `forwardRobotLinkWorldFrames`。Home 下这条链的连杆 4 在基座上方 (150, 0, 1200)，连杆 6 与 `link_6` 对齐。`build-full/Release/olp_tests.exe` 晚于上述源文件。审核方复跑：**657 passed, 0 failed**，退出码 0。`SiFastSuiteOLP.exe --verify` 退出码 0。`M11-007`、`M11-008` 关闭。

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M11-007 | P1 | **RESOLVED** | `TrajectorySimulator.cpp`<br>`OcctViewport.cpp`<br>`OcctViewportPlayback.cpp` | 播放和静止姿态都用 `forwardRobotLinkWorldFrames`。Home 连杆 4、连杆 6 的断言为 PASS。 |
| M11-008 | P1 | **RESOLVED** | `TimelinePanel.cpp` | `stopPlayback` 不再调用 `publish`。清理之后只改滑块和读数。 |
| M11-009 | P2 | **DEFERRED** | `tests/test_simulation.cpp` | 停止测试没有计划，`publish` 会直接返回。见 `DEFERRED.md`。 |
| M11-005 | P2 | **RESOLVED** | `OcctViewportPlayback.cpp` | 播放按 URDF 链摆七节网格。停止和 `invalidate()` 清掉 TCP 预览。 |

`RISK-001` 仍开着。第 6 步和第 7 步没有改。`OcctViewport.cpp` 640 行，`OcctViewportPlayback.cpp` 187 行，`TimelinePanel.cpp` 285 行，`TrajectorySimulator.cpp` 575 行，`FclCollisionQueries.cpp` 421 行，`tests/test_simulation.cpp` 488 行。

### M11-007 [P1] RESOLVED

`sampleTrajectory` 把 `linkWorldTransforms` 写成 `forwardRobotLinkWorldFrames(joints, robotBaseInWorld)`。`syncScene` 和 `syncRobotPosture` 用同一函数，基座是机器人节点的 `localTransform`。时间轴采样走 `PlaybackCursor::sample`，再交给 `setPlaybackRobotTransforms`。无交互上下文时也会建 `AIS_Shape` 并写入局部变换，读回用的是 `Transformation()`。

`forwardRobotLinkWorldFrames` 的六个原点仍是 (0, 0, 450)、(150, 0, 0)、(0, 0, 600)、(0, 0, 150)、(640, 0, 0)、(100, 0, 0) mm，J3 加上 J2，与碰撞查询同一条链。Home、基座 Z = 500 mm 时，连杆 4 在 (150, 0, 1700)，不是腕心 (790, 0, 1700)。Test 20 对七节的位置和姿态均为 PASS，连杆 4 的 1 mm 门槛和连杆 6 的 1° 门槛均为 PASS。

`CollisionAndSim.cpp` 里另一份扫掠帧仍写 OPW `linkTransforms`。时间轴不读它。示例导出在写出前改用 `TrajectorySimulator` 的帧。HTML 只画 TCP。

### M11-008 [P1] RESOLVED

`stopPlayback` 停表、把游标放回 0、调用 `clearPreview_`，然后在 `sliderGuard_` 为真时把滑块归零，有计划时只 `updateReadout()`。`invalidate()` 仍只清理一次。暂停不清理。播到结尾停在末帧。TCP 位置仍来自 OPW 的 `tcpPoseInBase`，不从连杆 6 的网格姿态取。

---

# 审核结论 — M11-PLAYBACK-SHAPE-001

> **Task ID**：`M11-PLAYBACK-SHAPE-001`
> **Task**：播放时用烘焙连杆变换驱动外形（DIR-001 第 5 步，M11-005）
> **Round**：1
> **Reviewer**：审核方
> **Reviewed at**：2026-10-28
> **Executor**：执行方
> **Verdict**：**CHANGES_REQUESTED**
>
> 审核方读了 `syncScene`、`setPlaybackRobotTransforms`、`stopPlayback` 和 `forwardRobotLinkWorldFrames`，并在 Home、全零、J2 = 20° 三处把 OPW `linkTransforms` 与 URDF 连杆链对过。`build-full/Release/olp_tests.exe` 晚于上述源文件。审核方复跑：**614 passed, 0 failed**，退出码 0。`SiFastSuiteOLP.exe --verify` 退出码 0。这三项新断言只比连杆 6 的位置，视口没有交互上下文，网格没有建出来。

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M11-007 | P1 | **OPEN** | `src/ui/OcctViewport.cpp`<br>`src/ui/OcctViewportPlayback.cpp`<br>`tests/test_simulation.cpp` | 七节 STL 被套上 OPW 抽象帧。Home 下连杆 4 偏 640 mm，连杆 6 姿态差 180°。 |
| M11-008 | P1 | **OPEN** | `src/ui/TimelinePanel.cpp` | 停止先清预览，再 `publish(false)` 把起点帧和 TCP 球画回去。 |
| M06-001 | P2 | **RESOLVED** | `src/device/RobotModel.cpp` | 内置模型不再写字面量关节原点。`urdfLoaded` 要求六个转动关节和 tool0 都读到。未加载时正解返回单位变换。 |
| M06-002 | P2 | **RESOLVED** | `tests/test_kinematics.cpp` | URDF 没打开时记失败并返回，不再解引用空模型。 |

`M11-005` 仍开着。`RISK-001` 仍开着。第 6 步和第 7 步没有改。

### M11-007 [P1] OPEN

`link_0`～`link_6` 的 STL 在 URDF 连杆坐标系里。碰撞查询用 `forwardRobotLinkWorldFrames`：原点依次是 (0, 0, 450)、(150, 0, 0)、(0, 0, 600)、(0, 0, 150)、(640, 0, 0)、(100, 0, 0) mm，轴向与 URDF 一致，J3 加上 J2。视口静止姿态和播放都用 `OpwKinematics::linkTransforms`。那一组是 OPW 抽象帧，连杆 6 对齐的是 tool0，不是 `link_6.stl`。

Home（J5 = −90°）下，连杆 0～2 重合。连杆 3 位置重合、姿态差 90°。连杆 4 的 URDF 原点在 (150, 0, 1200)，OPW 把它放在腕心 (790, 0, 1200)，相差 640 mm，姿态差 90°。连杆 5 位置重合、姿态差 90°。连杆 6 位置都在 (790, 0, 1100)，姿态差 180°。全零和只动 J2 时，连杆 4 仍差 640 mm，连杆 6 仍差 180°。

Test 20 只比较连杆 6 的位置，而且 `syncScene` 在没有交互上下文时直接返回，`robotLinkAis` 仍是空的。`playbackRobotTransforms()` 读回的是刚写进去的那组 OPW 帧。这三条断言在网格摆错时也会通过。

播放和场景静止姿态都要按当前关节角走与碰撞相同的 URDF 连杆链，再乘机器人基座。不要把 `fk.linkTransforms[k]` 套到 `link_k.stl` 上。回归至少在 Home 比较七节的位置和姿态：连杆 4 与 URDF 链的位置差要小于 1 mm，连杆 6 的姿态差要小于 1°。

### M11-008 [P1] OPEN

`clearPlaybackPreview` 会把七节改回 `sceneRobotLinkTransforms`，并去掉 TCP 球和轨迹。`invalidate()` 只走这一次清理。`stopPlayback` 在清理之后调用 `publish(false)`。`publish` 再执行 `setPlaybackTcp` 和 `setPlaybackRobotTransforms(起点帧)`。停止后 TCP 球还在，外形停在程序起点，不是清理时写下的场景关节。

暂停没有清理，末帧会留到停止，这两处保持。停止必须停在场景关节上，并且不再画出 TCP 球和轨迹。

### M06-001、M06-002 [P2] RESOLVED

`createFanucM10iA` 不再填写六个关节原点。`forwardUrdfFlange` 与 `forwardUrdfTool0` 在 `urdfLoaded` 为 false 时返回单位变换。`loadFromUrdf` 要 `revoluteParsedCount == 6` 且 `tool0Parsed` 才把 `urdfLoaded` 设为 true。Test 3 在加载失败时返回。

---

# 审核结论 — M06-KINEMATICS-URDF-001

> **Task ID**：`M06-KINEMATICS-URDF-001`
> **Task**：运动学金样改为从 URDF 读关节原点，并比较完整姿态（DIR-001 第 4 步）
> **Round**：1
> **Reviewer**：审核方
> **Reviewed at**：2026-10-28
> **Executor**：执行方
> **Verdict**：**APPROVED**
>
> 审核方读了 `fanuc_m10ia_standalone.urdf`、`RobotModel::loadFromUrdf`、`forwardUrdfTool0` 和 Test 3，并按文件里的 origin、axis、`link_6-tool0` 重算了同一组 200 个示教器角。`build-full/Release/olp_tests.exe` 的写入时间晚于这三份源文件。审核方复跑：**611 passed, 0 failed**，退出码 0。`SiFastSuiteOLP.exe --verify` 退出码 0。

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M06-001 | P2 | **DEFERRED** | `src/device/RobotModel.cpp` | 内置模型仍写着一套关节原点和 tool0 转角。`forwardUrdf*` 不看 `urdfLoaded`。见 `DEFERRED.md`。 |
| M06-002 | P2 | **DEFERRED** | `tests/test_kinematics.cpp` | URDF 打开失败后仍会解引用空模型。见 `DEFERRED.md`。 |

本轮没有新的 P0 或 P1。`RISK-001` 仍开着。第 5 步到第 7 步没有改。

零位链算到 (890, 0, 1200) mm，工具 Z 轴朝 +X。同一组 200 个示教器角上，位置差小于 \(10^{-12}\) mm，姿态差最大 \(0.000153^\circ\)。这个余量来自文件里的 `3.14159` 与 `-1.570796`，OPW 偏置用的是精确 \(\pm 180^\circ\) 和 \(-90^\circ\)。两条都低于 Test 3 的 0.01 mm 和 \(0.001^\circ\)。

`joint_6-flange` 的原点是单位变换，`link_6-tool0` 的父连杆是 `flange`。金样把 tool0 乘在 link_6 上，和把单位法兰接进去的结果相同。六个转动关节的 xyz 按米转毫米后是 (0, 0, 450)、(150, 0, 0)、(0, 0, 600)、(0, 0, 150)、(640, 0, 0)、(100, 0, 0)，轴向与文件一致。Test 3 在 `urdfLoaded` 为真之后调用 `forwardUrdfTool0`，比较的是位置、测地角、旋转矩阵九个分量和三个基向量。

198 组样本把 J5 固定在 \(-60^\circ\)，全零和只动 J2 的两组 J5 为 0。J4 与 J6 仍在变，完整旋转都参加了比较。示教器角到 URDF 角仍是 J3 加上 J2，系数 +1。`displayToUrdfJoints` 用的是同一个系数。没有示教器对拍，`RISK-001` 不关。

`RobotModel.h` 75 行，`RobotModel.cpp` 300 行，`tests/test_kinematics.cpp` 178 行。本轮 diff 没有改播放外形、工程 JSON 和 PDF。

---

# 审核结论 — M10-COLLISION-FCL-001

> **Task ID**：`M10-COLLISION-FCL-001`
> **Task**：M10 FCL 精确网格间隙替换球对包围盒、后台 72 组自转扫描与拖拽最新预览
> **Round**：2
> **Reviewer**：审核方
> **Reviewed at**：2026-10-28
> **Executor**：执行方
> **Verdict**：**APPROVED**
>
> 审核方读了 `queryRobotAndTorchCollision`、`GizmoDragPreviewHandler`、`prepareTwistScanTask` 和 `TargetPropertyPanel::showTarget`，并清点了拆分后的行数。`build-full/Release/olp_tests.exe` 由审核方复跑：**610 passed, 0 failed**，退出码 0。该二进制的写入时间晚于上述源文件。

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M10-FCL-001 | P1 | **RESOLVED** | `src/algo/FclCollisionQueries.cpp` | FCL 距离原样保留。包围盒只跳过相距超过 `max(当前最小间隙, 100 mm)` 的一对。 |
| M10-FCL-002 | P1 | **RESOLVED** | `GizmoDragPreviewHandler.cpp`<br>`TargetPropertyPanel.cpp`<br>`MainWindow.cpp` | 扫描和拖拽都在界面线程拷贝快照。拖拽逆解成功后查连杆 1～6 与四个焊枪球。忙标志放开后若序列号已前进，会再抢一次并算最新一帧。 |
| M10-FCL-003 | P1 | **RESOLVED** | `CollisionAndSim.cpp`<br>`ShellWindow.cpp` | 查询与拖拽已拆出。清点行数：764、423、811、172、189，都在 1000 行以内。 |
| M10-FCL-004 | P2 | **RESOLVED** | `FclCollisionQueries.cpp` | `Transit_Wall` 的调试打印已去掉。 |
| M10-FCL-005 | P2 | **DEFERRED** | `measureOperationTorchClearance` | 起弧内缩仍只查焊枪球。见 `DEFERRED.md`。 |

### M10-FCL-001 [P1] RESOLVED

`queryRobotAndTorchCollision` 的 FCL 分支把 `fcl::distance` 的返回值直接写入最小间隙。`pointToAabbSignedDistance` 只留在没有 FCL 的分支里。碰撞仍是 `outMinClearanceMm <= 0`。穿透时网格距离可以是 0，代码没有再换成包围盒中心的深度。

### M10-FCL-002 [P1] RESOLVED

`showTarget` 先在界面线程调用 `prepareTwistScanTask`，工人只跑 `executeTwistScanTask`，参数是已经拷好的 `TwistScanTask`。

拖拽回调在界面线程写入 `GizmoPreviewSnapshot`（位姿、场景节点、豁免名单、机器人模型）。工人读这份快照，用 `inverseKinematics` 与 `selectBestSolution` 求关节，再调用 `checkRobotAndTorchCollision`。逆解有解时，连杆 1～6 和四个焊枪球都参加。

工人在 `previewBusy` 写成 false 之后再看序列号。序列号已经变大时，用 `compare_exchange` 重新占用忙标志并继续循环。若界面线程已经抢走忙标志，本次循环退出，后启动的那次工人处理最新快照。

### M10-FCL-003 [P1] RESOLVED

`CollisionAndSim.cpp` 764 行，`FclCollisionQueries.cpp` 423 行，`ShellWindow.cpp` 811 行，`ShellChromeHelpers.cpp` 172 行，`GizmoDragPreviewHandler.cpp` 189 行。`CMakeLists.txt` 已把三个新的 cpp 放进对应目标。

## 本轮已核对并保持

- 四个焊枪球仍是半径 2、16、22、25 mm，沿 TCP 负 Z 的 0、45、95、150 mm。
- 焊接段对当前工件和豁免名单上的焊丝尖端，距离不小于 `-wireTipExemptionRadiusMm`（默认 10 mm）时不记入最小间隙。枪体球不豁免。
- 连杆位姿仍是 URDF 原点加 J2/J3 的 `+1`。`RISK-001` 仍开着。
- 逆解失败时拖拽状态栏仍显示焊枪球间隙，因为此时没有连杆位姿。可解的拖拽已经带上连杆。
- `有效 (14.8mm)` 与 M13 列宽不在本轮行为修改里。Test 21 的距离 0 没有写入 `RISK-002`。
- `M10-FCL-005` 仍延期：`measureOperationTorchClearance` 继续走 `checkTorchPoseCollision`。

---

# 审核结论 — M10-COLLISION-FCL-001 Round 1 记录

> **Task ID**：`M10-COLLISION-FCL-001`
> **Round**：1
> **Reviewed at**：2026-10-28
> **Verdict**：**CHANGES_REQUESTED**（当时结论。当前状态以上面的 Round 2 为准。）
>
> 审核方对照 `checkRobotAndTorchCollision`、`ShellWindow` 的拖拽回调和 `TargetPropertyPanel` 的扫描投递读了源码，没有把 `IMPLEMENTATION.md` 的通过说明当成证据。点位求解和扫掠已经把关节角送进 FCL。连杆位姿走的是 URDF 关节链，不是 OPW 抽象连杆。`Robot_Pedestal_Base` 因名字含 Pedestal 仍是夹具，没有被静态机器人那条跳过。M13 列宽和 `有效 (14.8mm)` 这条格式断言没有改，这条断言不读碰撞结果，保持原样是对的。

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M10-FCL-001 | P1 | **RESOLVED** | `src/algo/CollisionAndSim.cpp` | Round 1 发现：FCL 距离小于等于 0 时被改写成包围盒中心穿透深度。Round 2 已关闭。 |
| M10-FCL-002 | P1 | **RESOLVED** | `src/ui/ShellWindow.cpp`<br>`src/ui/TargetPropertyPanel.cpp`<br>`src/ui/MainWindow.cpp` | Round 1 发现：最后一次预览可能丢掉，拖拽不查连杆，后台直接读文档。Round 2 已关闭。 |
| M10-FCL-003 | P1 | **RESOLVED** | `src/algo/CollisionAndSim.cpp`<br>`src/ui/ShellWindow.cpp` | Round 1 发现：两个文件超过 1000 行。Round 2 已拆开并关闭。 |
| M10-FCL-004 | P2 | **RESOLVED** | `src/algo/CollisionAndSim.cpp` | Round 1 发现调试行。Round 2 已去掉。 |
| M10-FCL-005 | P2 | **DEFERRED** | `measureOperationTorchClearance` | 起弧内缩仍只查焊枪球，不查连杆。见 `DEFERRED.md`。 |

### M10-FCL-001 [P1] RESOLVED in Round 2

`checkRobotAndTorchCollision` 在 FCL 返回 `dist <= 0` 时，用连杆包围盒中心或球心到障碍包围盒的有符号距离覆盖这个结果。网格刚接触时，报出的深度可以是包围盒中心陷进去多深。点位缓存、问题列表和 PDF 状态列都读这个数。宽相位用包围盒丢掉远处一对是对的；接触之后的毫米数必须留下 FCL 的距离。非凸网格的距离在穿透时可以是 0，碰撞判断继续用 `<= 0`，不要再造一个负的包围盒深度。

### M10-FCL-002 [P1] RESOLVED in Round 2

拖拽回调先把 `previewBusy` 设为真，工人算完才清掉。清掉之前若又来了一次预览，新的序列号写进槽里，但 `compare_exchange` 失败，没有人再跑这一次。松手前的最后一帧可以停在上一次间隙上。

同一次预览调用的是 `checkTorchPoseCollision`，关节角是空的，连杆 1～6 不参与。状态栏上的间隙不是本轮要报的那组距离。

自转扫描和这次预览都在线程池里读 `document()`、场景节点和当前点。界面线程可以同时改这份文档。扫描和预览要在投递前把位姿、关节、场景网格和豁免名单拷出来，工人不要再碰文档。

### M10-FCL-003 [P1] RESOLVED in Round 2

`CollisionAndSim.cpp` 1143 行，`ShellWindow.cpp` 1040 行。两处都已经超过 1000 行，这次又把 FCL 查询和拖拽槽加了进去。按 `Rules.md`，继续改行为之前要先拆。碰撞查询拆到自己的编译单元；拖拽预览不要继续堆在 `ShellWindow.cpp` 里。拆分不改变对外结果。

---

## 本轮已核对并保持

- 四个焊枪球仍是半径 2、16、22、25 mm，沿 TCP 负 Z 的 0、45、95、150 mm。没有新的圆柱或锥体枪体。
- 焊接段只跳过当前工件和豁免名单上的焊丝尖端，阈值是 `wireTipExemptionRadiusMm`（默认 10 mm）。枪体球不豁免。
- 连杆网格用 `importRobotStlMillimeters`。关节链是 URDF 原点加 J2/J3 的 `+1`，和现有金样同一约定。`RISK-001` 仍开着。
- 点位求解、直线和关节扫掠、自转扫描在有关节角时会查连杆。72 组扫描在线程池里，并用 `cancelToken` 在换点和析构时取消。
- `tests/test_report.cpp` 的 `有效 (14.8mm)` 只检查状态文字格式。碰撞结果不经过它。M13 列宽不在本轮 diff 里。
- Test 21 的距离 0 没有写进 `RISK-002`。

# 审核结论 — M13-REPORT-PDF-001

> **Task ID**：`M13-REPORT-PDF-001`
> **Task**：M13 2D PDF 工位工艺指导报告导出、节拍统计与 UI 联动
> **Round**：8
> **Reviewer**：审核方
> **Reviewed at**：2026-10-27
> **Executor**：执行方
> **Verdict**：**APPROVED**
>
> Round 8 复审。审核方复跑 `build-full/Release/olp_tests.exe`：**606 passed, 0 failed**，退出码 0。满量程姿态 `-180.0, -180.0, -180.0` 宽 726，姿态格宽 730。构型格宽 580，`F D B, -1, -1, -1` 宽 561。两格之间空 4 像素，和事件格也空 4 像素。坐标列 850、状态列 413、焊缝摘要 516 没有改小。`M13-001` 关闭。

M12 的 `M12-001` ~ `M12-003`、`M12-005` 维持 **RESOLVED**。`M12-004`、`M12-006` 仍在 `DEFERRED.md`。M11、M10 已关闭项不改写。

---

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M13-001 | P1 | **RESOLVED** | `src/ui/WorkcellPdfReportExporter.cpp`<br>`tests/test_report.cpp` | 满量程姿态 726 对 730。构型 561 对 580。坐标、焊缝摘要和状态列保持上一轮宽度。 |
| M13-007 | P1 | **RESOLVED** | `WorkcellPdfReportExporter.cpp`<br>`src/ui/ShellWindow.cpp` | 规划长度改为末帧弧长减首帧弧长。测试跨距 200 mm，旧公式 195 mm，两者已分开。 |
| M13-002 | P1 | **RESOLVED** | `WorkcellPdfReportExporter.cpp`<br>`src/ui/SampleSession.cpp` | 示例 UFRAME 为 (500, −300, 600)，UTOOL Z 为 150，底座 Z 为 500。空文档写「无程序 / 未标定」。 |
| M13-003 | P1 | **RESOLVED** | `WorkcellPdfReportExporter.cpp` | 节拍 ≤ 0 时为「未仿真」，占空比 0。不再写入 38.51 / 33.31。 |
| M13-004 | P1 | **RESOLVED** | `WorkcellPdfReportExporter.cpp` | 九种状态各有文字。未检查为灰色，不再把其它状态画成绿色「有效」。 |
| M13-005 | P2 | **DEFERRED** | `tests/test_report.cpp` | 圆弧写成直线。多程序只留最后一个名字。见 `DEFERRED.md`。 |
| M13-006 | P2 | **DEFERRED** | `WorkcellPdfReportExporter.cpp` | 焊缝清单、问题清单和每条焊缝的离屏多视角仍未做。见 `DEFERRED.md`。 |
| M13-008 | P2 | **DEFERRED** | `src/ui/SampleSession.cpp` | 求解仍使用另一份 150 mm 字面量。坐标系编号固定写成 `[1]`。见 `DEFERRED.md`。 |

---

### M13-001 [P1] RESOLVED

300 DPI、10 mm 页边距下，可绘区域是 2243×3272。审核方在绘制用的 `QPainter` 上重测。姿态格从 12 起、宽 730，满量程 `-180.0, -180.0, -180.0` 是 726。构型格从 746 起、宽 580，`F D B, -1, -1, -1` 是 561，表头「构型 CONFIG」是 297。事件格从 1330 起、宽 903，三个事件「起弧[1] 熄弧[1] 摆焊[1] 」是 594。姿态格结束于 742，构型格结束于 1326，中间和后面各空 4 像素。

坐标列仍是 850，`-9999.9, -9999.9, -9999.9` 是 825。焊缝摘要文本框仍是 516，`999/99999.9mm` 是 494。状态列仍是 413，「有效 (998.0mm)」是 396。表头和数据行用的是同一组常量，姿态走 `formatTargetOrientation`。Test 20 对满量程姿态、两处分界和构型串均为 PASS。

当前范围内的 P1 都已关闭。`M13-005`、`M13-006`、`M13-008` 仍在 `DEFERRED.md`，不挡本次通过。

### M13-002 [P1] RESOLVED

`extractMetadata` 从坐标系节点、工具节点和机器人节点读取位姿。示例用户坐标系原点是 (500, −300, 600) mm，工具 `tcpOffset` 的 Z 是 150 mm，底座 Z 是 500 mm。空文档的程序名为「无程序」，没有工具或坐标系时写「未标定」。导出正文使用这份元数据。Test 20 这几条均为 PASS。

### M13-003 [P1] RESOLVED

`cycleTimeSec <= 0` 时 `isSimulated` 为 false，三张时间卡写「未仿真」，占空比为 0。源码里已经没有 38.51 和 33.31。传入 40 s 与 30 s 时，空走为 10 s，占空比 75%。Test 20 这几条均为 PASS。

### M13-004 [P1] RESOLVED

九种状态都走到 `formatTargetStatusAndClearance` 和 `getTargetStatusColor`。未检查是灰色「未检查 (-)」，不打印默认 1000 mm。有效和近碰撞带实测毫米数。碰撞、不可达、超限为红色。近限位、奇异和构型突变为橙色。绘制点位行时使用这两个函数。Test 20 对九种文字和颜色均为 PASS。

### M13-007 [P1] RESOLVED

`ShellWindow` 和 `extractMetadata` 都改成末帧 `arcLengthMm` 减首帧 `arcLengthMm`。采样帧少于 2 个时长度为 0。Test 20 用起点 32 mm、末点 232 mm 的焊缝，跨距是 200.0 mm，旧的「末帧再减两端切边」是 195 mm。报告长度与跨距之差小于 0.1 mm，并且和旧公式相差超过 1 mm。这两条均为 PASS。

---

## 本轮已核对并保持

- 文件菜单、保存对话框、状态栏和失败弹窗仍接通。快照仍是当前视口一张图。
- 点位 XYZWR 来自 `poseInUFrame.toWPR()`。工艺条件来自 `weldSchedules`，速度用 `getWeldSpeedMmPerSec()`。构型字符串仍是 `N U T, 0, 0, 0` 这一格式。
- 条件表、坐标表和点位表在越过页脚时会 `newPage()` 并重画表头。三张表的行高是 84，子行绘制矩形高 36。第 1 页坐标表结束在约 1826 像素，页脚线在页高减 55，两者没有叠上。
- `ShellWindow.cpp` 980 行，`WorkcellPdfReportExporter.cpp` 705 行，`tests/test_report.cpp` 680 行，均低于 1000 行。
- 求解函数里仍有一份独立的 `fromTranslation(0, 0, 150)`。坐标系表第一列固定写 `UTOOL[1]`、`UFRAME[1]`。这两条记在 `M13-008`，不挡上面三项关闭。

---

# 审核结论 — M12-POST-FANUC-001

> **Task ID**：`M12-POST-FANUC-001`
> **Task**：M12 FANUC .ls 后处理器完备化、多操作/多坐标系切换、工艺指令映射、合规校验门禁与 UI 导出联动
> **Round**：2
> **Reviewer**：审核方
> **Reviewed at**：2026-10-27
> **Executor**：执行方
> **Verdict**：**APPROVED**（当前 `STATUS.md` 范围）
>
> Round 2 复审。审核方复跑 `build-full/Release/olp_tests.exe`：**482 passed, 0 failed**，退出码 0。复跑 `SiFastSuiteOLP.exe --verify`：退出码 0，1161 帧，38.5067 秒。`M12-001`、`M12-002`、`M12-003` 关闭。顺带落地的最近整数把 `M12-005` 一并关闭。

M11 的 `M11-001` 维持 **RESOLVED**。`M11-002` ~ `M11-006` 仍在 `DEFERRED.md`。M10 已关闭项不改写。

---

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M12-001 | P1 | **RESOLVED** | `src/io/FanucLsPostProcessor.cpp`<br>`tests/test_postprocessor.cpp` | 速度先取最近整数。关节不在 1..100、直线不足 1 mm/s、CNT 不在 0..100 时记错误并拒绝导出。 |
| M12-002 | P1 | **RESOLVED** | `src/io/FanucLsPostProcessor.cpp`<br>`tests/test_postprocessor.cpp` | 点名超过 16 字符、程序注释超过 32 字符记错误。运动行和 `/POS` 不再截断。 |
| M12-003 | P1 | **RESOLVED** | `src/io/FanucLsPostProcessor.cpp`<br>`tests/test_postprocessor.cpp` | 关节运动带 `WELD_SPEED` 记错误并拒绝导出。关节行只写百分比。 |
| M12-004 | P2 | **DEFERRED** | `src/ui/LsExportDialog.cpp`<br>`src/ui/MainWindow.cpp` | 警告在通过时不显示。Home 两个勾选与正文不一致。对话框只处理第一段程序。见 `DEFERRED.md`。 |
| M12-005 | P2 | **RESOLVED** | `src/io/FanucLsPostProcessor.cpp` | 8.6 mm/s 写成 `9mm/sec`。向零截断已去掉。 |
| M12-006 | P2 | **DEFERRED** | `src/io/FanucLsPostProcessor.cpp`<br>`src/ui/LsExportDialog.cpp` | 头部日期、`FILE_NAME`、`PROG_SIZE`、关节行空格和控制器配置尚未按示教器样本收紧。见 `DEFERRED.md`。 |

---

### M12-001 [P1] RESOLVED

校验和生成都先 `std::round`。关节速度不在 1..100、非焊接直线速度不足 1 mm/s、CNT 不在 0..100 时记错误。`generateLsString` 在校验失败时返回空字符串，正文里不再把 0 改成 50% 或 200 mm/s，也不再把 CNT 夹到 100。

0.4% 和 0.4 mm/s 四舍五入后是 0，导出被拒绝。150% 和 CNT 250 同样被拒绝。8.6 mm/s 写成 `9mm/sec`。关节行在校验通过后只写这个整数百分比。Test 19 这几条均为 PASS。

### M12-002 [P1] RESOLVED

点名超过 16 字符、程序注释超过 32 字符记错误，`generateLsString` 返回空字符串。运动行和 `/POS` 直接写 `t.name`，没有 `substr`。`PROGRAM_NAME_16C` 与 `TARGET_NAME_16CH` 都是 16 个字符，对应的 17 字符样本被拒绝。程序注释用 32 个 `C` 通过、33 个 `C` 拒绝。Test 19 先断言长度再断言结果，这几条均为 PASS。

### M12-003 [P1] RESOLVED

`motion == J && useWeldSpeed` 记错误，生成函数在写运动行之前返回。关节分支只写百分比，`WELD_SPEED` 只出现在非关节运动上。Test 19 对这个组合断言校验失败且正文为空，结果为 PASS。

---

## 本轮已核对并保持

- 程序名规则与交接一致：大写字母开头，仅含大写字母、数字和下划线，长度 1..16。空程序、逆解无效、圆弧运动 `C` 会拒绝导出，不会把圆弧写成直线。
- 示例程序 `build/WELD_001.ls` 的段头、`UFRAME_NUM`/`UTOOL_NUM`、`Arc Start[1]`/`Arc End[1]`、`WELD_SPEED` 和 XYZWR 三位小数与现有 `toWPR()` 一致。配置字来自 `FanucConfig::toString()`，格式为 `N U T, 0, 0, 0`。
- `startState.homeJointsRad` 与机器人模型的 Home 同为示教器显示角。默认 Home 的 J2 为 0，按现有耦合式写出的 J3 与显示角相同。本条不重开 `RISK-001`。
- 多操作点号连续，`exportControllerPrograms` 写出两份文件。工具栏「导出」和文件菜单都会打开 `LsExportDialog`。`ShellWindow.cpp` 按编辑器行号为 948 行，交接里的 880 是非空行计数。各改动文件均低于 1000 行。
- 默认导出的回 Home 仍是注释行。勾选「Home 输出为 GP1 关节点」且同时保留回 Home 时，才会追加 `J P[n:HOME]` 和关节 `/POS`。该勾选组合的界面问题记在 `M12-004`，不挡上述语法。

---

# 审核结论 — M11-TRAJECTORY-001

> **Task ID**：`M11-TRAJECTORY-001`
> **Task**：M11 轨迹插补、节拍估算、帧烘焙与时间轴回放
> **Round**：3
> **Reviewer**：审核方
> **Reviewed at**：2026-10-27
> **Executor**：执行方
> **Verdict**：**APPROVED**（当前 `STATUS.md` 范围）
>
> Round 3 复审。审核方复跑 `build-full/Release/olp_tests.exe`：**425 passed, 0 failed**，退出码 0。复跑 `SiFastSuiteOLP.exe --verify`：退出码 0，1161 帧，38.5067 秒（焊接 33.305 秒，空走 5.20174 秒）。`CollisionAndSim.cpp` 与 `OpwKinematics.cpp` 无差异。`M11-001` 关闭。

M10 的 `M10-001` ~ `M10-003`、`M10-007` 维持 **RESOLVED**。`M10-004` ~ `M10-006`、`M10-008`、`M10-009` 仍在 `DEFERRED.md`。本轮不改写下方 M10 结论。

---

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M11-001 | P1 | **RESOLVED** | `src/algo/simulation/TrajectorySimulator.cpp`<br>`tests/test_simulation.cpp` | 直线采样、15° 判断、限速和播放共用短程增量。+180° 圈数对不上时，在同一手臂姿态里按短程增量选解。 |
| M11-002 | P2 | **DEFERRED** | `TrajectoryPlan::issues`<br>`src/ui/SampleSession.cpp`<br>`src/ui/TimelinePanel.cpp` | 圆弧说明和 15° 问题写在计划里。示例日志和问题列表仍只显示 M10。见 `DEFERRED.md`。 |
| M11-003 | P2 | **DEFERRED** | `src/ui/TimelinePanel.cpp` | 已有播放计划时，选中另一段程序不会重算。见 `DEFERRED.md`。 |
| M11-004 | P2 | **DEFERRED** | `TrajectorySimulator.cpp` | 直线采样上限 240，长路径会粗于 5 mm / 2°。见 `DEFERRED.md`。 |
| M11-005 | P2 | **DEFERRED** | `src/ui/OcctViewportPlayback.cpp` | 播放只画 TCP 球和轨迹，连杆网格不随帧摆动。见 `DEFERRED.md`。 |
| M11-006 | P2 | **DEFERRED** | `src/ui/TimelinePanel.cpp` | 按操作分项的时间已算进计划，时间轴只显示总时间、焊接时间和空走时间。见 `DEFERRED.md`。 |

---

### M11-001 [P1] RESOLVED

直线采样用 `shortestJointStep` 把关节折成相对上一采样的短程角。15° 判断、梯形限速和 `jointsAt` 读这组增量。段末用同一次判断对齐终点。关节运动仍写入终点主值的原始差值。

锁定圈数找不到解时，`samePostureByShortStep` 在翻转、肘、肩相同且未超限位的候选里，按短程增量的平方和选取，然后再做 15° 判断。超过 15° 时问题句仍是 `Linear segment joint step exceeds 15 degrees`。

Test 18 覆盖 J6 从 179° 到 −179°、从 −179° 到 +179°，以及 J4 从 −170° 到 +170°、从 +170° 到 −170°。后三条断言不报无逆解、不置 `hasConfigJumpInLinear`、相邻关节步小于 15°，每个采样的正运动学落在该点球面插值上（0.5 mm、1° 以内）。审核方复跑这三项均为 PASS。Round 2 里 J4 负侧折点停在 −178°、偏差 3.47 mm 的路径已经消失。

---

## 本轮已核对并保持

- 关节运动六轴共用一条梯形路径参数，同时起停。50% 速度比 100% 更慢。Test 18 的梯形数值与 `makeTrapezoid` 一致。
- 直线运动在采样点上做位置线性插值和姿态球面插值，逆解锁终点构型。焊接段的构型锁与 M10 相同：起点到达后锁定，熄弧后放开。
- 起弧、熄弧在到达之后生效。焊接时间只计已经起弧的那段运动。总时间等于焊接加空走，并按操作累加。
- 烘焙含 0、约 30 FPS 网格、总时长和各段终点。帧上的连杆变换与正运动学一致。
- 播放、暂停、停止、按点、步进 0.1 秒、倍速、拖动时先暂停、从选中点起播，都接到 `PlaybackCursor`。未到结尾时播放不倒回；到结尾再播才回到程序起始关节。停止回到 0。倍速夹在 0.1 到 8。
- 播放只改视口里的 TCP 球和轨迹，点动读数写「仿真（未写入程序）」。不写机器人节点，不入命令栈。文档刷新会 `invalidate()` 并清掉预览。
- 圆弧记一条 `Circular motion is approximated as a straight line in this estimate`，然后按直线估算。拐角各自停稳，没有混合。
- 示例会话仍用 `validateAndSimulateProgram` 决定问题和退出码，再用 M11 的帧和三项时间覆盖导出。
- `ShellWindow.cpp` 935 行。新的仿真、时间轴、视口预览和 Test 18 各自成文件，都低于 1000 行。

---

# 审核结论 — M10-COLLISION-REACH-001

> **Task ID**：`M10-COLLISION-REACH-001`
> **Task**：M10 可达性、奇异点、碰撞与自转角扫描 — M10-007 复审
> **Round**：2
> **Reviewer**：审核方
> **Reviewed at**：2026-10-26
> **Executor**：Gemini / Claude
> **Verdict**：**APPROVED**（当前 `STATUS.md` 范围）
>
> 对照提交 `70bd8f8`。审核方复跑 `build-full/Release/olp_tests.exe`：**368 passed, 0 failed**。`M10-001` ~ `M10-003` 维持 **RESOLVED**。`M10-007` 关闭。

M09 的 `M09-001` ~ `M09-003` 维持 **RESOLVED**。`M10-004` ~ `M10-006`、`M10-008` 仍在 `DEFERRED.md`。本结论不覆盖这些延期项，也不表示 M11 已开始。

---

## 结论

| ID | 级别 | 状态 | 涉及文件 | 结论 |
|---|---|---|---|---|
| M10-001 | P1 | **RESOLVED** | `src/ui/MainWindow.cpp`<br>`src/app/CommandManager.cpp` | 入栈之后才返回是否变更。自转快照含整份 cache。选中点的关节会写回机器人节点。 |
| M10-002 | P1 | **RESOLVED** | `src/ui/MainWindow.cpp`<br>`src/algo/CollisionAndSim.cpp` | 已有解时锁定 `cache.solvedConfig`。焊接角色豁免焊丝尖端，接近点不豁免。 |
| M10-003 | P1 | **RESOLVED** | `src/ui/ShellWindow.cpp` | 问题列表含段内扫掠与构型跳变，条目带 `targetId`、`operationId`。 |
| M10-007 | P1 | **RESOLVED** | `src/ui/MainWindow.cpp`<br>`src/ui/ShellWindow.cpp` | 撤销、重做和入栈都会重算当前程序的全部点 cache 与 `lastSimulationResult`。已删除的点和操作不再留在问题列表里。 |

---

### M10-007 [P1] RESOLVED

`MainWindow` 在构造时注册文档监听。`execute`、`undo`、`redo` 入栈或弹出之后都会 `notifyDocumentChanged()`。监听里的 `handleDocumentChanged()` 对当前操作所在的整段程序调用 `validateOperation`，写回每个点的关节、构型、状态、间隙和问题文本，并替换 `lastSimulationResult_`。`inValidation_` 挡住这次验证再进入监听。`validateOperation` 本身不再发文档通知，不会套娃。

监听注册在界面刷新之前。同一轮通知里先重算，`ShellWindow` 再 `fillIssues()`。撤销后的列表读到的是恢复姿态上的那次仿真。

选中操作已被删掉时，选择和 `lastSimulationResult_` 被清空；文档里不再有操作时也不再保留旧问题。`fillIssues()` 对找不到的操作或目标点直接跳过。

Test 17 在夹具仍在场时记录下游点 2、点 3 的关节、构型和 `issues` 文本，再把起点自转 20°。撤销后这三项与自转前的记录一致，重做后与自转后的记录一致。继续撤销到生成操作消失后，`issues` 为空，问题列表里没有已删除点的 `targetId`。

被编辑点自己的姿态和关节仍由命令快照带回，监听只是按恢复后的姿态再解一次。这次重解与编辑前那次验证一致，所以 `M10-001` 的撤销对称仍然成立。
