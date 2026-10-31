# 审核结论 — M14-PROJECT-REBIND-002 Round 1

> **Task ID**：`M14-PROJECT-REBIND-002`  
> **Round**：1  
> **Reviewer**：独立审核方（只读源码与本机产物；未改 `src/`、`include/`、`tests/`）  
> **Reviewed at**：2026-10-31  
> **Verdict**：**APPROVED**  
> **P0**：无  
> **P1**：无  
> **P2**：`M14-014`、`M14-015` 已入 `DEFERRED`。`M14-012`、`M14-013` **RESOLVED**

## 1. 结论

两条延期项可以关闭。脏文档取消打开时，当前工程和 `isDirty` 都保持原样。确认放弃后才换成新工程。关闭窗口走同一条确认。打开工程会先清空 CAD 内核，再按 `sourceCadFile` 重绑句柄；文件不在或导入失败时句柄写成 -1，网格还在。

## 2. M14-012 [RESOLVED]

- `confirmDiscardIfDirty`（`src/ui/ShellWindow.cpp:620-634`）：文档不脏直接放行。脏文档优先走 `setConfirmDiscardCallback`；没钩子才弹出 `QMessageBox`，默认按钮是取消。
- `openProjectFile`（`:649-652`）在选文件和 `openProject` 之前返回。取消时不调用打开。
- `closeEvent`（`:636-642`）取消时 `ignore()`。
- 测试在 `OLP_HAS_QT6` 内。`LastTest.log:921-934` 十四条 PASS：取消保持 `nodeA` 且仍为脏；确认后换成 `nodeB` 且脏标记清除；不脏时钩子不被调用；关闭取消与确认各一条。

## 3. M14-013 [RESOLVED]

- `OcctCadKernel::clear`（`src/cad/OcctCadKernel.cpp:76-84`）清掉 `solids_`、`shapes_`、`edgeShapes_`、`faceShapes_`，并把 `nextHandleId_` 置回 1。头文件里的缓存成员都在这里。
- `MainWindow::openProject`（`src/ui/MainWindow.cpp:778-854`）：JSON 装入失败时直接返回，内核不动。装入成功后先 `clear()`。机器人节点跳过，构造时 `cadHandleId` 默认 -1，视口也不用它取 B-rep。其余节点：`sourceCadFile` 为空、路径不存在或导入失败，写成 -1。路径用 `utf8ToPath`，先绝对路径，再工程目录相对路径。同一规范化路径只 `importStep` 一次。同名实体取该文件里的句柄；文件里只有一个实体时用那一个。
- 视口（`src/ui/OcctViewport.cpp:394-403`）在句柄取不到形状时画 `renderMesh`。
- `LastTest.log:935-948`：旧句柄在打开后 `getEdges` 为空；丢失 STEP 的句柄为 -1，渲染网格 4 个顶点、碰撞网格 2 个三角形都还在；相对路径 `TestPart.step` 的两个节点重绑到大于 0 且相等的新句柄，边非空。两个节点用的是同名，相等句柄同时证明了按名匹配和同文件只导入一次。

## 4. 本机实跑

`Tools\Compile.bat` exit 0。`build/Testing/Temporary/LastTest.log:980` 为 `Summary: 862 passed, 0 failed.` `--verify` exit 0。

编辑器行号（含空行）：`ShellWindow.cpp` 993，`MainWindow.cpp` 856，`OcctCadKernel.cpp` 884，`test_project_io.cpp` 811。交接写的 854 与 804 偏低，都没到 1000。`ShellWindow.cpp` 再加功能前要先按职责拆开。

`git diff` 对 `include/olp/algo` 与 `src/algo` 为空。

## 5. 未纳入本条关闭范围

`加载示例工位` 不走 `confirmDiscardIfDirty`。重绑定把 `std::filesystem::path::string()` 交给 `importStep`，而 `importStep` 内部不用 `utf8ToPath`。见 `M14-014`、`M14-015`。

---

# 审核结论 — MAINT-CLEANUP-P2-001 Round 2

> **Task ID**：`MAINT-CLEANUP-P2-001`  
> **Round**：2  
> **Reviewer**：独立审核方（重读 Round 2 源码与本机产物；未改 `src/`、`include/`、`tests/`）  
> **Reviewed at**：2026-10-31  
> **Verdict**：**APPROVED**  
> **P0**：无  
> **P1**：`MAINT-001` **RESOLVED**  
> **P2**：`MAINT-003` **RESOLVED**。`MAINT-002`、`FIX-EXPORT-P2-D` 仍为 `DEFERRED`

## 1. 结论

`MAINT-001` 已关闭。一段程序只记一个带前缀的键。id 非空时键是 `id:` 加 id，不再把 `name` 放进同一个集合做或匹配。`prog_1` 与 `prog_2` 同名 `WELD_001` 时，只仿真第一段，第二段的 const 校验和预览仍报 `no fresh simulation`。

## 2. MAINT-001 [RESOLVED]

- `programCanonicalKey`（`src/ui/MainWindow.cpp:554-562`）：id 非空返回 `"id:" + id`；否则 name 非空返回 `"name:" + name`；两者都空才用 `"addr:"` 加对象地址。
- `markProgramEnsured`（`:564-566`）只插入这一个键。`isProgramEnsured`（`:568-570`）只查这一个键。
- 前缀把「一段的 id 等于另一段的 name」分开：`"id:prog_1"` 与 `"name:prog_1"` 不是同一个键。
- 测试 `tests/test_postprocessor.cpp:565-624`：A 为 `prog_1` / `WELD_001`，B 为 `prog_2` / `WELD_001`，场景里有夹具包围盒。只 `ensureProgramSimulatedForExport("prog_1")`。`LastTest.log:638-641` 四条均为 PASS：ensure 成功、`prog_1` 不再报缺失仿真、`prog_2` 的 validate 与 preview 都含 `no fresh simulation`。
- 空 id 且名字相同的两段仍共用 `name:` 键。const 接口按 `findProgramInDoc` 也只能命中第一段。这是 Round 1 验收里写明的回退，不另开问题。地址键在 vector 重新分配后对不上，结果是误判为未仿真，门禁更严。

## 3. MAINT-003 [RESOLVED]

`src/ui/SampleSession.cpp:10` 包含 `olp/io/Utf8Path.h`。拒写分支 `:231-232` 调用 `std::filesystem::remove(olp::io::utf8ToPath(lsOut), ec)`。`exportToFile` 使用同一函数（`include/olp/io/Utf8Path.h:11`，`src/io/FanucLsPostProcessor.cpp:400`）。`ec` 仍不检查；路径对象已经与写出一致。

## 4. 本机实跑

`Tools\Compile.bat` exit 0。`build/Testing/Temporary/LastTest.log:952` 为 `Summary: 834 passed, 0 failed.` `--verify` exit 0。

编辑器行号（含空行）：`MainWindow.cpp` 790，`SampleSession.cpp` 273，`test_postprocessor.cpp` 693，`CommandManager.cpp` 477，`ToolpathCommands.cpp` 528。全部低于 1000。

`git diff` 对 `include/olp/algo` 与 `src/algo` 为空。

## 5. 仍延期

`MAINT-002`：`ensureProgramSimulated`（`:589-618`）找到程序后仍恒返回 true，`exportAllPrograms:702-711` 的失败分支到不了。`FIX-EXPORT-P2-D` 未改。

---

# 审核结论 — MAINT-CLEANUP-P2-001 Round 1

> **Task ID**：`MAINT-CLEANUP-P2-001`  
> **Round**：1  
> **Reviewer**：独立审核方（只读源码与本机产物；未改 `src/`、`include/`、`tests/`）  
> **Reviewed at**：2026-10-31  
> **Verdict**：**CHANGES_REQUESTED**  
> **P0**：无  
> **P1**：`MAINT-001` **OPEN**  
> **P2**：`MAINT-002`、`MAINT-003`、`FIX-EXPORT-P2-D` 已入 `DEFERRED`

## 1. 结论

`P2-A` 没有闭环。`markProgramEnsured` 把 `id` 和 `name` 写进同一个集合，`isProgramEnsured` 命中任意一个就返回真。`core::Program::name` 默认就是 `WELD_001`。第一段程序仿真过后，另一段 id 不同、名字仍是 `WELD_001` 的程序，`const validateProgramForExport` 和 `previewFanucLs` 不再报 `no fresh simulation`。

本轮测试只覆盖了空 id、名字分别为 `PART_A` 和 `PART_B` 的情况。830 条全绿盖不住这条。

## 2. MAINT-001 [OPEN] 仿真记账把不同程序当成同一段（P1）

- **位置**：`src/ui/MainWindow.cpp:554-575`；默认名 `include/olp/core/ProjectModel.h:308`；示例程序 `src/ui/SampleSession.cpp:126-127`；测试 `tests/test_postprocessor.cpp:78-79`、`:466-470`、`:548-563`。
- **现状**：`markProgramEnsured` 在 id 非空时插入 id，在 name 非空时再插入 name，最后再插入 `"prog_addr_"` 加对象地址。`isProgramEnsured` 对这三样做或判断。注释写的是“这一段程序”（`:578-580`），判断不是按这一段。
- **反例**：程序 A 为 `id=prog_1`、`name=WELD_001`（与示例程序相同）。程序 B 为 `id=prog_2`、`name=WELD_001`（默认名，只改了 id）。对 A 调用 `ensureProgramSimulated` 后，集合里有 `prog_1` 和 `WELD_001`。`validateProgramForExport("prog_2")` 找到 B，B 的 name 已在集合里，`exportSimulationMissingOrStale` 返回 false，门禁放行。B 的 id 等于 A 的 name 时同样放行。
- **测试为什么是绿的**：`testProg` 只改了 `name`，id 保持空。`PART_A` 与 `PART_B` 两个字符串不同，`:557` 和 `:562` 能拦住。没有“不同 id、相同 name”的断言。
- **写路径**：`exportFanucLs`（`:679`）和 `exportAllPrograms`（`:706-707`）仍会重新仿真。漏洞在本任务要收口的 const 校验和预览。
- **验收**：一段程序只记一个键。id 非空时不得再靠 name 命中，键要带前缀，避免一段的 id 等于另一段的 name。新增断言：A 为 `prog_1` / `WELD_001` 且已 ensure，B 为 `prog_2` / `WELD_001` 且未仿真，`validateProgramForExport("prog_2")` 与 `previewFanucLs("prog_2")` 仍含 `no fresh simulation`。

## 3. 本轮核对通过的部分

命令拆分按职责切开。`ModifyTargetPose`、`ReverseOperationPath`、`SmoothTargetOrientations`、`ModifyTargetProcessParams`、`RotateTargetAroundTcpZ` 只在 `src/app/ToolpathCommands.cpp` 实现，`CommandManager.cpp` 中无这些符号。声明仍在 `include/olp/app/CommandManager.h`。`CMakeLists.txt:136` 已把新编译单元加入 `olp_app`。`ToolpathCommands.cpp` 以完整的 `mergeWith` 结束，没有从函数中间截断。

编辑器行号（含空行）：

| 文件 | 行号 |
| :--- | ---: |
| `src/app/CommandManager.cpp` | 477 |
| `src/app/ToolpathCommands.cpp` | 528 |
| `include/olp/app/CommandManager.h` | 336 |
| `include/olp/ui/MainWindow.h` | 166 |
| `src/ui/MainWindow.cpp` | 795 |
| `src/ui/SampleSession.cpp` | 272 |
| `tests/test_postprocessor.cpp` | 632 |
| `CMakeLists.txt` | 224 |

全部低于 1000。1500 硬上限未触及。

`exportAllPrograms` 对当前 `core::Program&` 调用 `ensureProgramSimulated`（`:706-707`），不再按字符串反查。这是 `P2-B` 的写路径，本轮接受。

`SampleSession.cpp:229-231` 在拒写正式 `.ls` 时调用 `std::filesystem::remove`。本机路径为 ASCII，`--verify` exit 0。`P2-C` 的删除动作本轮接受。中文路径见 `MAINT-003`。

`git diff` 对 `include/olp/algo` 与 `src/algo` 为空。`OpwKinematics` 未改。`olp_algo` 仍不链 Qt。

## 4. 本机实跑

`Tools\Compile.bat` exit 0。`build/Testing/Temporary/LastTest.log:948` 为 `Summary: 830 passed, 0 failed.` `SiFastSuiteOLP.exe --verify` exit 0，写出 `build/WELD_001.ls` 与 `build/WELD_001_Preview.html`。

全绿不构成 `MAINT-001` 已关闭的证据。

## 5. 交接里不成立的收口

`DEFERRED.md` 把 `FIX-EXPORT-P2` 写成 A/B/C 全部关闭，并删掉了 `P2-D`。A 即 `MAINT-001`，仍开放。`P2-D` 代码未动：`src/algo/CollisionAndSim.cpp:41-53` 把 `Singularity` 和 `NearJointLimit` 放在 Soft，段内 `NearCollision` 仍可经 `shouldUpgradeCacheStatus`（`:37-65`，调用点 `:19-25`）盖掉端点奇异状态。两者在后处理里都是 Warning，不升为 P1。已放回 `DEFERRED`，编号 `FIX-EXPORT-P2-D`。

`ensureProgramSimulated`（`:594-622`）找到程序后恒返回 true。`exportAllPrograms:707-716` 的 `ensure failed` 分支到不了。记为 `MAINT-002`。

---

# 审核结论 — M13-HTML3D-EXPORT-001 Round 2

> **Task ID**：`M13-HTML3D-EXPORT-001`  
> **Round**：2  
> **Reviewer**：独立审核方（重读 Round 2 源码与本机产物；未改 `src/`、`include/`、`tests/`）  
> **Reviewed at**：2026-10-31  
> **Verdict**：**APPROVED**  
> **P0**：无  
> **P1**：`M13-HTML3D-001`、`M13-HTML3D-002` 均 **RESOLVED**  
> **P2**：仍为 `DEFERRED`，本轮未要求修改

## 1. 结论

两条 P1 已关闭。Home 帧枪尖与 TCP 重合，距离 0 mm。焊接帧距离 0.0105 mm，来自四位小数打印，低于 0.05 mm 断言，也低于 Round 1 的 1 mm 验收。

## 2. 本机实跑

`Tools\Compile.bat` exit 0。`build/Testing/Temporary/LastTest.log:945` 为 `Summary: 827 passed, 0 failed.` `--verify` exit 0。

`build/WELD_001_Preview.html` 338,107 字节。无 `http://`、`https://`。交接写的 282,143 字节是旧产物。

## 3. M13-HTML3D-001 [RESOLVED]

- `computeFlangeToTool0`（`src/io/HtmlAnimationDataPrep.cpp:193-204`）在 `urdfLoaded == false` 时写入 `RotY(-π/2)·RotX(π)`，与 `fanuc_m10ia_standalone.urdf:69-70` 的 tool0 固定旋转一致。`createFanucM10iA` 的 `urdfLoaded` 为 false，导出走这条矩阵。
- `makeWeldingTorchMeshInLink6`（`:207-268`）先在 tool0 里沿 `utoolOffset.pos` 挤出，再对每个顶点做 `flangeToTool0.transformPoint`。`tcpLocal` 是同一点。
- 播放仍乘 `linkWorldTransforms[6]`（`src/io/HtmlAnimationExporter.cpp:521`）。
- 产物：`tcpLocal=150,0,0`。焊枪顶点包围盒 `[0,-24,-24]..[150,24,24]` mm，枪身沿 link_6 的 +X，半径约 24 mm，末端在 X=150。
- Home：`jDeg` 的 J5 为 -90°，`tcp=(790,0,1450)`，link6 原点 `(790,0,1600)`，列 0 为 `(0,0,-1)`。`150·列0` 落到 `(790,0,1450)`。距离 **0.000000 mm**。
- 焊接帧 `tcp=(812,-150,765)`，同一 `tcpLocal` 变换后距离 **0.010496 mm**。
- 测试 `tests/test_report.cpp:786-876` 从 HTML 解析矩阵再算距离，阈值 0.05 mm。`LastTest.log:835` 与 `:837` 为 PASS。它核的是 `tcpLocal`，不是网格顶点；顶点轴向由上面的包围盒单独核对。

## 4. M13-HTML3D-002 [RESOLVED]

编辑器行号（含空行），不是交接里的去空行数字：

| 文件 | 行号 |
| :--- | ---: |
| `src/io/HtmlAnimationExporter.cpp` | 611 |
| `src/io/HtmlAnimationDataPrep.cpp` | 501 |
| `src/io/HtmlAnimationDataPrep.h` | 88 |
| `tests/test_report.cpp` | 882 |
| `src/ui/ShellWindow.cpp` | 957 |

全部低于 1000。1500 硬上限未触及。新编译单元已进 `CMakeLists.txt:121`。拆分按网格/帧准备与页面拼装切开，没有从函数中间截断。抽稀与操作搜索仍在 `HtmlAnimationDataPrep.cpp:349-498`，逻辑与 Round 1 通过的部分一致。

## 5. 硬约束

`git diff` 对 `OpwKinematics` 与 `src/algo`、`include/olp/algo` 为空。`olp_algo` 仍不链 Qt。

## 6. P2

`M13-HTML3D-003` 至 `006` 保持 `DEFERRED`。播放器仍不在关键帧之间插值。本轮不打开。

---

# 审核结论 — M13-HTML3D-EXPORT-001 Round 1

> **Task ID**：`M13-HTML3D-EXPORT-001`  
> **Round**：1  
> **Reviewer**：独立审核方（只读源码、diff 与本机产物；未改 `src/`、`include/`、`tests/`）  
> **Reviewed at**：2026-10-31  
> **Verdict**：**CHANGES_REQUESTED**  
> **P0**：无  
> **P1**：2（`OPEN`）  
> **P2**：4（已转入 `CRDocs/DEFERRED.md`，本轮不要求修）

## 1. 结论

导出链能编译、能写出离线 HTML，测试套件是绿的。绿测试没有挡住焊枪轴向错误。

`build/WELD_001_Preview.html`（281,811 字节）第一帧是 Home（`J5 = -90°`）。法兰原点在 `(790, 0, 1600)` mm，TCP 在 `(790, 0, 1450)` mm，连线沿世界 `-Z`。焊枪网格挂在 `link_6` 上，沿该连杆的 `+Z` 伸出，世界方向是 `+X`。两方向点积为 0，夹角 90°。枪尖落在 `(940, 0, 1600)`，离 TCP 约 212 mm。

这是功能错误，阻断本轮通过。

## 2. 本机实跑

`Tools\Compile.bat`（Release）exit 0。

- `ctest`：`SiFastSuiteOLP_AllTests` Passed（4.59 s）。`build/Testing/Temporary/LastTest.log:939` 为 `Summary: 821 passed, 0 failed.`
- `SiFastSuiteOLP.exe --verify` exit 0。产物 `build/WELD_001_Preview.html`，281,811 字节。全文无 `http://`、`https://`，无 `<script src>`，无 `<link href>`。
- `rawFrameCount: 1047`，`keyframeCount: 184`。起弧/熄弧在保留帧之间翻转 2 次。

821 条通过只说明字符串断言没有失败。第 8 节没有比较枪尖和 TCP。

## 3. 取证表

| # | 审核项 | 证据 | 判定 |
| :--- | :--- | :--- | :--- |
| K1 | OPW 腕部重合帧会重算成 URDF 连杆系 | `src/io/HtmlAnimationExporter.cpp:250-265,279-280`。OPW 的 link4/link5 同在腕心（`src/algo/OpwKinematics.cpp:113-126`），`d45 < 1e-3` 且 `d23 = c2 = 600`。`TrajectorySimulator` 已经写入 `forwardRobotLinkWorldFrames`（`src/algo/simulation/TrajectorySimulator.cpp:444`），`d45 ≈ 640`，不会误重算。`1e-0` 就是 `1.0`，对角元阈值仍是 `1e-6` | **PASS** |
| K2 | 7 节连杆矩阵与 STL 同在 URDF 连杆系 | `src/algo/FclCollisionQueries.cpp:225-248` 的原点/轴与 `fanuc_m10ia_standalone.urdf:36-63` 一致。网格用单位阵导出，再乘 `linkWorldTransforms[k]`（`HtmlAnimationExporter.cpp:616-617,950-952`）。Home 帧 link6 原点 `(790,0,1600)` 与手算一致 | **PASS**（连杆） |
| K3 | 焊枪挂在 link6 且沿 UTOOL 指向 TCP | 网格沿 `utoolOffset.pos` 挤出（`HtmlAnimationExporter.cpp:149-156,626-631`），播放时乘 `linkWorldTransforms[6]`（`:954`）。OPW 法兰对的是 tool0，不是 link6（`tests/test_kinematics.cpp:151-154`；URDF `link_6-tool0` 的 `rpy="3.14159 -1.570796 0"`，`urdf:69-70`）。tool0 的 +Z 落到 link6 的 +X。产物 `tcpLocal=[0,0,150]`，枪沿 link6 +Z | **FAIL → P1 `M13-HTML3D-001`** |
| K4 | 二进制 STL：小端、`84+n*50`、米→毫米、坏文件回退 | `HtmlAnimationExporter.cpp:84-132,135-147`。本机 `link_6.stl`：376 三角形，18884 = 84+376*50。包围盒约 `[-0.0125,-0.031,-0.031]..[0,0.031,0.031]` 米，`×1000` 正确。文件打不开或长度不符则 `makeFallbackRobotLinkMesh` | **PASS** |
| K5 | 焊枪正交基：平行 X、零长度 | `HtmlAnimationExporter.cpp:149-156`。`norm() ≤ 1` 换成 `(0,0,150)`。`|dir·X| > 0.9` 改用 Y，叉乘模长 ≥ sin(arccos 0.9) ≈ 0.44，不会除零 | **PASS**（构造本身）。父坐标系仍然是 K3 |
| K6 | 相机、近裁剪、背面剔除、深度排序 | `HtmlAnimationExporter.cpp:863-903,957`。yaw=0、pitch=0 时 forward=`(-1,0,0)`、right=`(0,1,0)`、up=`(0,0,1)`，Z-up 右手系。`zCam ≤ 20` 丢弃该顶点。Y 向下屏幕里 `area2 ≥ 0` 剔除，与外向 CCW 一致。`b.z - a.z` 远的先画 | **PASS** |
| K7 | 0.1 mm 量化、退化三角形、越界索引 | `HtmlAnimationExporter.cpp:209-245`。工位尺度内（约 ±100 m）键唯一。退化点 `i0==i1` 丢弃。索引越界 `continue`。超出约 `[-100000, +109715]` mm 时 21-bit 掩码回绕 | **PASS**（工位内）/ **P2**（回绕） |
| K8 | 0/1/2 帧、全静止、起熄弧 | `HtmlAnimationExporter.cpp:288,297-334`。`size ≤ 2` 原样返回。首尾帧保留。`arcActive` 与相邻保留帧或下一原始帧不同则保留。产物 1047→184，弧状态翻转仍为 2 次 | **PASS** |
| K9 | 操作搜索越界、禁用操作、空帧 | `HtmlAnimationExporter.cpp:651-715`。`!op.enabled` 直接 `continue`。空 `decimatedFrames` 不按下标读时间。`fi < size` | **PASS** |
| K10 | 未仿真就导出是否吃到旧的 `lastSimulationResult_` | `src/ui/MainWindow.cpp:706-740` 每次调用 `TrajectorySimulator::plan`，不读 `lastSimulationResult_`。基座、UFRAME、工具都从当前文档取 | **PASS**（见 P2：工具字段与时间轴不一致） |
| K11 | 引号、反斜杠、换行、中文 | `escapeJson` `HtmlAnimationExporter.cpp:19-34`；`escapeHtmlText` `:37-49`。`"` `\` `\n` `\r` `\t` 有处理。UTF-8 中文原样写入，`<title>` 已转义 | **PASS**（这四类）/ **P2**（`</script>`） |
| K12 | 播放器 0 帧、1 帧、拖动、倍速、跳转 | `HtmlAnimationExporter.cpp:757-758,832-849,1021-1031`。0 帧不索引 `bakedFrames`。1 帧不推进。滑条按索引取值，`-1` 跳转被拒绝。倍速 `parseFloat \|\| 1`。抽稀后两关键帧之间保持上一帧，不插值 | **PASS**（不越界）/ **P2**（不插值） |
| H1 | `OpwKinematics` 零 diff | `git diff -- include/olp/algo/OpwKinematics.h src/algo/OpwKinematics.cpp` 为空 | **PASS** |
| H2 | `olp_algo` 无 Qt | `include/olp/algo`、`src/algo` 无 Qt 头。`CMakeLists.txt:100-113` 只链 `olp_core` / `olp_cad` / `olp_device` / `fcl`。本轮 diff 不含 `src/algo` | **PASS** |
| H3 | 单文件行数 | 编辑器行号：`HtmlAnimationExporter.cpp` **1044**。交接写的是 947。其余改动文件：头 33、`MainWindow.h` 161、`MainWindow.cpp` 766、`ShellWindow.h` 123、`ShellWindow.cpp` 957、`SampleSession.cpp` 269、`test_report.cpp` 789。均低于 1000。1500 硬上限没有文件触及 | **FAIL → P1 `M13-HTML3D-002`** |
| H4 | 离线、中文路径、体积 | 产物无 URL。路径走 `utf8ToPath`（`HtmlAnimationExporter.cpp:86,398`）。281,811 字节 `< 50 MB`。中文路径断言在 `tests/test_report.cpp:690-699`，含在 821 通过里 | **PASS** |

## 4. P0

无。编译通过。空帧、坏 STL、空路径有回退或返回 `false`。`OpwKinematics` 未改。1500 行硬上限未破。

## 5. P1（OPEN，本轮必须修）

### M13-HTML3D-001 [RESOLVED in Round 2] 焊枪网格的父坐标系是 link_6，UTOOL 向量在 tool0 系

- **位置**：`src/io/HtmlAnimationExporter.cpp:149-181`（沿 `utoolOffset.pos` 挤出），`:626-631`（单位阵烘焙），`:954`（乘 `linkWorldTransforms[6]`）。对照 `src/algo/FclCollisionQueries.cpp:241-247`、`src/algo/OpwKinematics.cpp:128-135`、`SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Robot_FANUC_M10iA/urdf/fanuc_m10ia_standalone.urdf:61-70`。
- **事实**：`forwardRobotLinkWorldFrames` 的第 7 个姿态是 URDF `link_6`。金样把 OPW 法兰对到 `forwardUrdfTool0`（`tests/test_kinematics.cpp:151-154`）。`tool0` 相对 `link_6` 的固定旋转把工具 +Z 映到 link_6 的 +X。`link_6.stl` 是一块沿 X 只有约 12.5 mm 厚、YZ 约 ±31 mm 的法兰片，薄向就是工具轴。
- **产物**：`build/WELD_001_Preview.html` 第一帧 `jDeg=[0,0,0,0,-90,0]`，`tcp=[790,0,1450]`，link6 齐次矩阵末 12 个数为 `[0,0,1, 790, 0,1,0, 0, -1,0,0, 1600]`。列向量 Z = `(1,0,0)`，原点 `(790,0,1600)`。`tcpLocal=[0,0,150]` 因此枪尖在 `(940,0,1600)`。TCP 相对原点是 `(0,0,-150)`。夹角 90°，距离约 212 mm。这个固定旋转每一帧都在，不限于 Home。
- **验收**：Home 与至少一帧非零关节下，枪尖（网格沿工具轴的末端）与该帧 `tcp` 距离 `< 1 mm`。连杆 STL 仍乘 link_6，不要把整条手臂改乘 tool0。测试必须算这个距离，禁止只搜索 `Welding_Torch` 字符串。

### M13-HTML3D-002 [RESOLVED in Round 2] `HtmlAnimationExporter.cpp` 已过 1000 行拆分门禁，交接行数是错的

- **位置**：`src/io/HtmlAnimationExporter.cpp:1044`。`.claude/Rules.md` 单文件行数：到 1000 行后，下一次加行之前必须按职责拆编译单元。1500 是硬上限。
- **事实**：编辑器行号 1044（含空行）。`CRDocs/IMPLEMENTATION.md` 写成 947，并写「全部低于 1000」。差 97 行。
- **验收**：按职责切开（页面拼装与网格/关键帧准备分开即可）。禁止把一个函数从中间截断。新 `.cpp` 写入 `CMakeLists.txt` 的 `olp_io`（现为 `CMakeLists.txt:119-124`）。拆完后每个文件编辑器行号 `< 1000`。行为与 `M13-HTML3D-001` 的枪尖断言保持一致。

## 6. P2（DEFERRED，不挡本轮修复清单）

已写入 `CRDocs/DEFERRED.md`。执行者本轮不要顺手改这些。

- **M13-HTML3D-003**：播放器在两个关键帧之间保持上一帧（`HtmlAnimationExporter.cpp:1021-1031,1007-1009`）。抽稀只在「线性偏离 ≤ 容差」时删帧（`:319-330`），播放端没有插值。本机长间隔（≥ 0.2 s，142 段）最大 TCP 跳动 2.43 mm / 0.14°。快的直线空走可以更大。
- **M13-HTML3D-004**：`quantizeKey`（`:209-215`）低 21 位回绕，安全区大约 `[-100000, +109715]` mm。`escapeJson`（`:19-34`）不处理 `</script>`。`std::setprecision(2)`（`:433`）把旋转矩阵元素收成 0.01。
- **M13-HTML3D-005**：`tests/test_report.cpp:709-776` 以子串存在当作几何正确。`keyframeCount != 21` 盖不住「起弧帧被删」。枪尖错误在 821 通过时仍然存在。
- **M13-HTML3D-006**：`exportHtmlAnimation`（`src/ui/MainWindow.cpp:717-725`）按工具号读 `tcpOffset`，时间轴和校验读的是第一个工具的 `localTransform`（`:354-358`，`src/ui/TimelinePanel.cpp:145-148`）。示例里两者同为 Z=150，所以本轮对得上。菜单传入的程序号是空串（`src/ui/ShellWindow.cpp:202`），多程序时导出第一段。导出器里的 UFRAME 取最后一个同号节点（`HtmlAnimationExporter.cpp:422-423`），`computeUFrameInWorld` 取第一个（`src/app/CommandManager.cpp:129-135`）。

## 7. 交给执行者

只修 `M13-HTML3D-001` 与 `M13-HTML3D-002`。P2 保持 `DEFERRED`。修完后重新跑 `Tools\Compile.bat`，并在 `IMPLEMENTATION.md` 里给出 Home 帧枪尖与 TCP 的毫米差。

---

# 审核结论 — FULL REAUDIT Gemini

> **Task ID**：`FIX-EXPORT-GEMINI-FULL-REAUDIT`  
> **Task**：`3b977e9`（相对父提交 `19cb1ac`）`FIX-EXPORT-SAFETY` `GATE-001..006` 累计改动独立全盘复审  
> **Round**：FULL REAUDIT Gemini（Round 1）  
> **Reviewer**：Gemini（Si_FastSuite 独立审核方；逐文件重读源码与 diff 取证，不复用旧结论当证据）  
> **Reviewed at**：2026-10-31 19:13 Asia/Shanghai (UTC+8)  
> **Repo & Scope**：`C:\Users\mimovr\Desktop\TEmp\Si_FastSuite` | `git diff 19cb1ac..3b977e9`（26 files changed）  
> **设计权威**：`Docs/OLP_System_Design_Claude.md` v1.1（M10/M12、算法层禁 Qt、ROBOGUIDE 验收约束）  
> **计划依据**：`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md`（状态 **CLOSED**）  
> **工程约束**：本轮未修改任何产品/测试源码（`src/`、`include/`、`tests/`、`OpwKinematics` 零改动），仅更新 `CRDocs/`  
> **Compile/test（本机实跑）**：**已在本机 Windows 复跑 `Tools\Compile.bat` 通过**（MSVC 17.14 / Qt 6.12.0 / OCCT 8.0.0 / FCL Release 构建 exit 0；`SiFastSuiteOLP_AllTests` **805 passed, 0 failed**，`build/Testing/Temporary/LastTest.log:901`；`SiFastSuiteOLP.exe --verify` exit 0）。不再仅依赖口头采信。

---

## 1. Verdict

- **正式 Verdict**：**APPROVED**（`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md` 计划范围内无新发现 P0/P1，既有 `OPEN-P1-1..4` 与 `OPEN-P2-GATE005-1` 均已核实闭环）。
- **风险口径**：**同意协调方 `PASS_WITH_RISKS` 的风险分层判断**——默认门禁与写出前强制重仿真主链闭合无击穿，但主线运动学对拍 `RISK-001` 仍为 `OPEN`（水印与红条仅作草稿警示，不等于修正运动学），且存在少量不阻断安全主链的 P2 边界项。
- **主计划状态**：`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md` **仍应保持 CLOSED**（严禁因 P2 重开计划）。

---

## 2. 独立取证表（Claim → File:Line → PASS/FAIL）

| # | 审核项（Claim） | 源码与测试独立取证（`file:line`） | 判定 |
| :--- | :--- | :--- | :--- |
| **G1** | 默认开启 `blockOnCollision` / `blockOnConfigJump` | `include/olp/io/FanucLsPostProcessor.h:15-16`（两者默认初始值均为 `true`）；`include/olp/ui/LsExportDialog.h:29`（`config_{}` 默认构造）；`src/ui/LsExportDialog.cpp:61-62`（`chkBlockCollision_->setChecked(true)`） | **PASS（CLOSED）** |
| **G2** | 默认配置下 `Collision` / `ConfigJump` 报 `Error` 并拒写 `.ls` | `src/io/FanucLsPostProcessor.cpp:112-121`（`Collision` → `blockOnCollision ? Error : Warning`）、`:123-132`（`ConfigJump` → `blockOnConfigJump ? Error : Warning`）、`:207`（`result.ok = !result.hasErrors()`）、`:215-226`（`!valResult.ok` 时 `generateLsString` 返回 `{}`）、`:393-397`（`body.empty()` 时 `exportToFile` 在创建目录/打开文件前返回 `false`） | **PASS（CLOSED）** |
| **G3** | `NearCollision` 不短路同段后续硬碰撞采样；`sweepCollided` 写回 `cache.status = Collision` 且不改 `cache.valid` | `src/algo/CollisionAndSim.cpp:283-284`（拆分 `postedHardSweepCollision` 与 `postedNearSweepIssue` 双布尔旗）、`:357`（扫掠循环仅由 `!postedHardSweepCollision` 短路，`postedNearSweepIssue` 不短路后续 step）、`:364-373`（`sweepCollided` 时置硬撞旗并调用 `upgradeTargetCacheFromSegmentIssue(..., Collision, msg)`）、`:19-28`（仅更新 `status`/`issueMessage`，第 26 行明确保留 `cache.valid` 不变） | **PASS（CLOSED）** |
| **G4** | Hard 同级内 `Collision` 优先升级（`shouldUpgradeCacheStatus`） | `include/olp/algo/CollisionAndSim.h:135-137`（公开静态方法声明）；`src/algo/CollisionAndSim.cpp:37-65`（Soft=0 < Near=1 < Hard=2；第 60-63 行：当 `inTier == curTier == 2` 且 `incoming == Collision && current != Collision` 时返回 `true`，确保段内后发的 `Collision` 可覆盖先发的 `ConfigJump`/`Unreachable`/`JointLimitExceeded`，反之不降级） | **PASS（CLOSED）** |
| **G5** | Gate-off（`blockOnCollision=false`）时 `Collision` 降为 `Warning`，UI 禁止纯绿掩盖 | `src/io/FanucLsPostProcessor.cpp:112-121`（`blockOnCollision=false` 时仍推入 `Severity::Warning` 而非静默丢弃）；`src/ui/LsExportDialog.cpp:143-165`（`valResult.ok` 时若 `!valResult.issues.empty()` 走 `#e5c07b` 黄色 `⚠ 校验通过（含警告，可导出）` 分支并逐条列出 `issuesHtml`；仅 `issues.empty()` 时显示 `#98c379` 绿色通过）；`tests/test_postprocessor.cpp:214-232` | **PASS（CLOSED）** |
| **G6** | 导出/预览刷新前强制 `ensure`；`const` validate/preview 缺仿真时拒止；`exportAllPrograms` 逐程序 `ensure` | `src/ui/MainWindow.cpp:530-549`（`documentHasCollisionRelevantGeometry`）、`:551-564`（`exportSimulationMissingOrStale`）、`:566-599`（`ensureProgramSimulatedForExport`：有碰撞几何时无条件重跑 `validateAndSimulateProgram` 并置 `exportSimEnsured_=true`）、`:19`（`handleDocumentChanged` 重置 `exportSimEnsured_=false`）、`:609-621` & `:631-638`（`const` 校验/预览在 stale 时拒止）、`:647`（`exportFanucLs` 先 `ensure`）、`:669-693`（`exportAllPrograms` 逐程序 `ensure`，失败则记错并 `continue` 跳过该控制器）；`src/ui/LsExportDialog.cpp:122,215` | **PASS（CLOSED）** |
| **G7** | 共享 `utf8ToPath` 支持 Windows 中文路径导出 | `include/olp/io/Utf8Path.h:11`；`src/io/Utf8Path.cpp:19-31`（`_WIN32` 下用 `MultiByteToWideChar(CP_UTF8, ...)` 转 `std::wstring` 构造 `filesystem::path`）；`CMakeLists.txt:123`；`src/io/FanucLsPostProcessor.cpp:2,400,431,437-444`（保持 UTF-8 字符串拼接后交 `exportToFile`）；`src/io/ProjectSerializer.cpp:6,803,872`；`tests/test_postprocessor.cpp:378-400`（本机 Windows 实跑通过） | **PASS（CLOSED）** |
| **G8** | `RISK-001` `/MN` 水印 + 对话框常驻红条；`OpwKinematics` 零 diff；`olp_algo` 无 Qt | `src/io/FanucLsPostProcessor.cpp:241`（`addMnLine("  ! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001)")` 作为 `/MN` 第 1 行，`:328` `LINE_COUNT` 同步计入；本机产物 `build/WELD_001.ls:10,20` 已核验）；`src/ui/LsExportDialog.cpp:32-42`（独立 `riskBanner` 置顶，`refreshPreview` `:114-179` 零触碰）；`git diff 19cb1ac..3b977e9 -- include/olp/algo/OpwKinematics.h src/algo/OpwKinematics.cpp` 输出为空；`git grep -E "Qt\|QObject\|QString\|Q_" -- include/olp/algo src/algo` 无匹配 | **PASS（CLOSED）** |
| **G9** | `SampleSession` 校验或阻断级仿真问题失败时不写新 `.ls` | `src/ui/SampleSession.cpp:206-216`（汇总 `blockingIssue`）、`:222-223`（`refuseProductionLs = !lsVal.ok \|\| blockingIssue`）、`:228-240`（`refuseProductionLs` 为真时不调用 `exportToFile`，`lsOk=false`）、`:243-251`（返回 `exitCode = 2`） | **PASS（CLOSED）** |
| **G10** | 单测锁覆盖（Test 9 / 9b / 9c / Test 19 GATE-001..006） | `tests/test_collision.cpp:100-195`（Test 9）、`:198-284`（Test 9b 含 `:274` `check(sawNear, ...)`）、`:287-309`（Test 9c Hard 升级矩阵）；`tests/test_main.cpp:46-48`；`tests/test_postprocessor.cpp:207-418,504-610` | **PASS（805/0 本机实跑全绿）** |

---

## 3. 怀疑式端到端推演痕迹（End-to-End Traces）

### Trace 1：端点无碰、段中穿障 → 写回 `target.cache` → 默认导出拒止
1. `CollisionAndSimEngine::validateAndSimulateProgram`（`src/algo/CollisionAndSim.cpp:234-260`）：目标端点先做 IK 与碰撞检测，端点无碰时 `target.cache.valid = true`、`target.cache.status = Valid`。
2. 进入段内插补循环（`:286-399`）：当 `step < numSteps && !postedHardSweepCollision && doSweepCheck`（`:357`）时执行 `checkRobotAndTorchCollision`。
3. 若第 $k_1$ 步先进入安全距离内（`sweepClearance < collisionSafeClearanceMm`），触发 `:374-387`：`postedNearSweepIssue = true`，`upgradeTargetCacheFromSegmentIssue` 将 `target.cache.status` 从 `Valid`（Tier 0）升为 `NearCollision`（Tier 1）。**注意 `postedHardSweepCollision` 仍为 `false`**。
4. 后续第 $k_2$ 步发生硬碰撞（`sweepCollided == true`），仍能进入 `:357` 分支并触发 `:364-373`：`postedHardSweepCollision = true`，`shouldUpgradeCacheStatus(NearCollision, Collision)` 返回 `true`（`2 > 1`），`target.cache.status` 升为 `Collision`，`target.cache.valid` 保持 `true`。
5. 若同段在硬碰撞之前先在 `:327-340` 触发了 `>15°` 的 `ConfigJump`（Tier 2），第 $k_2$ 步硬碰撞时 `shouldUpgradeCacheStatus(ConfigJump, Collision)` 因 `:60-63` 同级 `Collision` 优先规则返回 `true`，状态仍升为 `Collision`。
6. 调用 `FanucLsPostProcessor::exportToFile(prog, path)`（默认 `LsPostConfig{}`）：内部 `generateLsString` → `validateProgram` 在 `:112-121` 命中 `t.cache.status == Collision && config.blockOnCollision` → 产生 `Severity::Error` → `valResult.ok = false` → `generateLsString` 返回空串 → `exportToFile` 在 `:394-396` 立即返回 `false`，磁盘零写入。**链路完全闭合。**

### Trace 2：场景/障碍物变更或加载未仿真文档后直接请求导出
1. 若经过 `CommandManager` 修改文档，`MainWindow::handleDocumentChanged()`（`src/ui/MainWindow.cpp:19`）立即将 `exportSimEnsured_ = false`。
2. **关键深层核查**：即便外部代码直接修改 `mainWin.document().sceneNodes`（绕过 `CommandManager`，未触发 `handleDocumentChanged`），当调用非 `const` 写出入口 `MainWindow::exportFanucLs`（`:647`）、`MainWindow::exportAllPrograms`（`:676`）或打开/刷新 `LsExportDialog::refreshPreview`（`src/ui/LsExportDialog.cpp:122`）时，它们都会调用 `ensureProgramSimulatedForExport(programId)`。
3. 查看 `MainWindow::ensureProgramSimulatedForExport`（`src/ui/MainWindow.cpp:566-599`）：只要 `documentHasCollisionRelevantGeometry()` 为真，**该函数不会因为 `exportSimEnsured_ == true` 或 `lastSimulationResult_.frames` 非空而跳过重算**，而是每次都对目标 `prog` 重新执行 `simEngine.validateAndSimulateProgram(*prog, ...)` 刷新 `target.cache`！
4. 因此，无论用户通过命令栈还是直接改文档、无论单程序还是多程序，**所有非 `const` 导出与对话框预览入口都天然具备 per-program 实时重仿真保护，不存在陈旧 `Valid` cache 漏放硬碰撞的击穿路径**。

### Trace 3：用户在 UI 主动取消勾选「阻断碰撞点导出」
1. `LsExportDialog::refreshPreview`（`src/ui/LsExportDialog.cpp:118`）将 `config_.blockOnCollision = false`（但 `config_.blockOnConfigJump` 仍保持默认 `true`）。
2. `validateProgram`（`src/io/FanucLsPostProcessor.cpp:112-121`）将 `Collision` 记为 `Severity::Warning`，`valResult.ok == true` 且 `!valResult.issues.empty()`。
3. 对话框进入 `src/ui/LsExportDialog.cpp:150-157`：状态栏显示黄色 `⚠ 校验通过（含警告，可导出）` 并逐条列出黄色碰撞警告；顶部 `riskBanner`（`:32-42`）继续常驻红条警告；导出的 `.ls` 正文 `/MN` 第 1 行（`src/io/FanucLsPostProcessor.cpp:241`）仍带 `! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001)`。**无纯绿静默掩盖。**

---

## 4. 点名抽核三项（独立裁决）

### 抽核 1：文档级 freshness（非 per-program）是否仍是 P2 / 是否应升 P1？
- **结论**：**仍是 P2，不应升为 P1。**
- **理由与行号**：
  1. `exportSimEnsured_`（`include/olp/ui/MainWindow.h:150`）与 `lastSimulationResult_`（`:148`）确为 `MainWindow` 文档级状态，`exportSimulationMissingOrStale()`（`src/ui/MainWindow.cpp:551-564`）不区分 `programId`。在多程序控制器下，先对 `PART_A` 执行 `ensure` 后，再对未仿真的 `PART_B` 调用 `const` 方法 `validateProgramForExport("PART_B")`（`:601`）或 `previewFanucLs("PART_B")`（`:625`），不会触发 `"no fresh simulation"` 拒止，同时令 `tests/test_postprocessor.cpp:592-601` 对 `PART_B` 的断言无法单独证明 `PART_B` 跑过 `ensure`。
  2. **但为什么绝不构成 P1**：如上文 Trace 2 所证，`ensureProgramSimulatedForExport`（`src/ui/MainWindow.cpp:566-599`）在存在碰撞几何时**不读取 `exportSimEnsured_` 作短路**，而是每次实跑 `validateAndSimulateProgram`。所有真实导出入口（`exportFanucLs` `:647`、`exportAllPrograms` `:674-686`、`LsExportDialog::refreshPreview` `LsExportDialog.cpp:122`、`LsExportDialog::exportToFile` `:215`）均无条件调用 `ensureProgramSimulatedForExport`。故真实写出链与 UI 预览链已是 per-program 重算，不存在安全门禁击穿。

### 抽核 2：GATE-006 `ensure-fail → skip` 负路径
- **结论**：**维持 P2（防御性近似死分支 + 负路径测弱，非功能缺陷）。**
- **理由与行号**：
  1. `MainWindow::exportAllPrograms`（`src/ui/MainWindow.cpp:674-689`）遍历 `cu.programs` 并传入 `key = !prog.id.empty() ? prog.id : prog.name` 调用 `ensureProgramSimulatedForExport(key)`。
  2. 而 `ensureProgramSimulatedForExport`（`:567-570`）仅在 `findProgramInDocMutable(doc_, key) == nullptr` 时返回 `false`。由于被遍历的 `prog` 就在 `doc_.controllers` 内，正常状态下必能命中（甚至当 `prog.id` 和 `prog.name` 均为空串时，`key == ""` 也会在 `:522` 因 `programId.empty()` 命中首个程序并返回 `true`，随后在 `exportControllerPrograms` → `validateProgramName` `FanucLsPostProcessor.cpp:13` 因空程序名被拒写）。
  3. **补充细微发现（同归入 P2-B）**：若跨控制器存在同名且无 `id` 的两个程序，或某程序 `id`/`name` 双空，`findProgramInDocMutable(doc_, key)`（`:519-528`）会按顺序命中文档中第一个匹配项而非当前 `cu` 下的 `prog` 引用。若后续直接传 `core::Program&` 重载给内部 helper 可彻底消除该查找分歧并简化代码。当前因程序名校验与单控制器结构不构成 P1。

### 抽核 3：`SampleSession` 不删磁盘旧 `WELD_001.ls`
- **结论**：**维持 P2。**
- **理由与行号**：
  1. `src/ui/SampleSession.cpp:228-251` 在 `refuseProductionLs == true` 时不调用 `exportToFile` 并以 `exitCode = 2` 失败退出（`Tools/Compile.bat:185-190` 会随之报错返回非零），严格兑现了 `PlanDocs/FIX-EXPORT-SAFETY-PLAN.md:29`「失败返回或隔离文件名，二选一，优先失败返回」的验收要求。
  2. 拒写分支未调用 `std::filesystem::remove(lsOut)` 清理历史构建遗留的 `build/WELD_001.ls`，仅在「先前构建曾成功生成过 `.ls`、后续修改造成阻断且调用方忽略 `exitCode = 2` 直接去 `build/` 目录拷文件」时才可能误用旧文件。属构建产物清理卫生项，定为 P2。

---

## 5. Findings 汇总（P0 / P1 / P2）

### P0（阻断级）
- **无**。

### P1（当前计划必修级）
- **无**。原交叉审计提出的 `OPEN-P1-1..4` 均已在 `GATE-005` 中闭环。

### P2（可延期项 — 已同步登记至 `CRDocs/DEFERRED.md` `FIX-EXPORT-P2`）
- **P2-A（文档级 freshness）**：`MainWindow.h:150` / `MainWindow.cpp:551-564` `exportSimEnsured_` 与 `lastSimulationResult_` 未按 `programId` 记账，多程序下 `const` `validateProgramForExport`/`previewFanucLs` 可能对未仿真的第二程序免除 stale 提示（且 `test_postprocessor.cpp:592-601` 断言对 `PART_B` 独立证明力偏弱）。
- **P2-B（GATE-006 查找与负路径测试）**：`MainWindow.cpp:674-689` 通过字符串 `key` 反查 `findProgramInDocMutable`，导致 `ensure-fail -> skip` 分支在正常遍历下不可达且缺负路径单测；若存在跨控制器重名或 `id`/`name` 双空程序，`findProgramInDocMutable` 会命中首个程序（建议未来增加直接接受 `core::Program&` 的内部 `ensure` 重载）。
- **P2-C（SampleSession 旧文件清理）**：`SampleSession.cpp:228-237` 拒写时不主动删除磁盘上可能已存在的旧 `build/WELD_001.ls`。
- **P2-D（Soft 层状态覆盖）**：`CollisionAndSim.cpp:41-53` 将 `Singularity` / `NearJointLimit` 归为 Soft（Tier 0），段内 `NearCollision`（Tier 1）会覆盖端点 `Singularity` 的 `cache.status`（两者在后处理中均为 `Warning`，不影响阻断门禁）。
- **主线 `RISK-001`（保持 OPEN，非本计划 P0/P1）**：`OpwKinematics` 的 J2/J3 耦合、圈数轴与法兰位姿尚未经过 ROBOGUIDE / 真机金样对拍；本计划落地的 `/MN` 水印与对话框红条仅完成草稿门禁，不关闭运动学风险本身。

---

## 6. 与既有协调方 / `Audit_X` 报告的同意与分歧

| 议题 | 协调方（`FULL_REAUDIT_COORDINATOR`） | `Audit_X`（`FULL REAUDIT Audit_X`） | Gemini 本轮裁决与理由（含 `file:line`） |
| :--- | :--- | :--- | :--- |
| **总体结论与风险口径** | `PASS_WITH_RISKS`（无新 P0/P1，有 P2 与 `RISK-001`） | `APPROVED`（同意 `PASS_WITH_RISKS` 风险分层） | **同意双方**。按 `CRDocs/README.md` 状态机，当前范围无 OPEN P0/P1 应记 `APPROVED`；风险实质同意 `PASS_WITH_RISKS`。 |
| **主链 1..8 闭合性** | 全部 PASS | 全部 CLOSED/OK | **同意**。已独立核对 `FanucLsPostProcessor.{h,cpp}`、`CollisionAndSim.{h,cpp}`、`MainWindow.{h,cpp}`、`LsExportDialog.{h,cpp}`、`SampleSession.cpp`、`Utf8Path.{h,cpp}`。 |
| **点名抽核 1（P2-A 文档级 freshness）** | 定为 P2（写出路径有 per-program ensure） | 同意 P2，并指出 `PART_B` 断言偏弱 | **同意定为 P2**，并补充了关键代码机制证明：`ensureProgramSimulatedForExport`（`MainWindow.cpp:566-599`）不看 `exportSimEnsured_` 短路，每次必重跑 `validateAndSimulateProgram`，彻底排除写出链漏仿真的可能。 |
| **点名抽核 2（GATE-006 负路径）** | 定为 P2（死代码/测弱） | 同意 P2 | **同意定为 P2**，并额外指出 `MainWindow.cpp:519-528,675` 按 `key` 反查在跨控制器同名或 `id`/`name` 双空时会命中首程序的设计瑕疵（可通过直接传 `Program&` 根治）。 |
| **点名抽核 3（SampleSession 旧 `.ls`）** | 定为 P2 | 同意 P2 | **同意定为 P2**（`SampleSession.cpp:228-251` 已返回 `exitCode = 2` 满足计划项 1）。 |
| **编译与 805/0 测试证据** | Linux box 未复跑，仅采信 Windows 805/0 声称 | Linux box 未复跑，仅采信声称 | **本轮升级为实跑验证（消除先前两份报告的未复验短板）**：本机 Windows 直接运行 `Tools\Compile.bat` 编译通过，`SiFastSuiteOLP_AllTests` 实测 **805 passed, 0 failed**（`build/Testing/Temporary/LastTest.log:901`），`SiFastSuiteOLP.exe --verify` exit 0。 |
| **DEFERRED.md 同步性** | 称残留 P2 跟踪在 REVIEW 与 DEFERRED | 同左 | **指出并补齐文档缺口**：此前 `SessionBrief.md:26` 与 `.claude/Plan.md:34` 均声称 FIX-EXPORT 残留 P2 见 `CRDocs/DEFERRED.md`，但 `DEFERRED.md` 实际未写入对应条目；本轮已在 `CRDocs/DEFERRED.md` 正式补入 `FIX-EXPORT-P2 [OPEN]`。 |

*End of FULL REAUDIT Gemini.*

---

# 审核结论 — FULL REAUDIT Audit_X

> **Task ID**：`FIX-EXPORT-FULL-REAUDIT`  
> **Task**：FIX-EXPORT GATE-001..006 累计改动全盘独立复审（与协调方交叉验证）  
> **Round**：FULL REAUDIT  
> **Reviewer**：Si_FastSuite_Audit_X（执行臂；独立取证，**不复用**旧 Round / 协调方行号当证据）  
> **Reviewed at**：2026-10-29 ~22:30 Asia/Singapore (UTC+8)  
> **Repo**：`/workspace/Si_FastSuite`（working tree 相对 `HEAD` `19cb1ac`；`git fetch/status/pull` 已同步，branch = `origin/main`）  
> **设计权威**：`Docs/OLP_System_Design_Claude.md` v1.1（M10/M12 / 算法层无 Qt / ROBOGUIDE 验收已对照）  
> **约束**：只写 CRDocs；**未改**工程业务/测试源码；禁止 CloudAgent；主计划 FIX-EXPORT-SAFETY **仍 CLOSED**（本轮无新 P0）  
> **Compile/test**：box **未**编译/复跑 ctest。Windows **`SiFastSuiteOLP_AllTests` 805 passed / 0 failed** 采信 STATUS/IMPLEMENTATION/协调方声称；**Audit_X 本端未复跑**。

## Verdict

**APPROVED**（按本任务规则：无新 P0/P1 → APPROVED；主计划保持 CLOSED）。

风险口径上 **同意** 协调方 `PASS_WITH_RISKS`：**是** — 默认写出主链门禁成立，但仍有已文档化 P2 + 主线 RISK-001 OPEN + 805/0 未复跑；不定零风险干净 PASS，亦不定 CHANGES_REQUESTED。

| 项 | 结论 |
| :--- | :--- |
| 新 P0 | **无** |
| 新 P1 | **无** |
| 主计划 `PlanDocs/FIX-EXPORT-SAFETY-PLAN.md` | **仍 CLOSED**（勿重开） |
| 协调方 Verdict `PASS_WITH_RISKS` | **同意其风险分层**；本端正式 Verdict = **APPROVED** |
| Windows 805/0 | **采信声称**；Audit_X **未复跑** |

---

## 方法（独立、禁止复用旧结论）

1. `git fetch` / `status` / `pull --ff-only`；基线 `HEAD=19cb1ac`；累计未提交 diff 为审核面。  
2. 通读对照（**不作证据**）：`CRDocs/FULL_REAUDIT_COORDINATOR_2026-10-29.md`、`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md`、`CROSS_AUDIT_FINAL_COORDINATOR.md`、`IMPLEMENTATION.md`、`STATUS.md`。  
3. **重新** `Read`/`rg`/`git diff`：`FanucLsPostProcessor.{h,cpp}`、`CollisionAndSim.{h,cpp}`、`Utf8Path.{h,cpp}`、`ProjectSerializer.cpp`、`LsExportDialog.cpp`、`MainWindow.{h,cpp}`、`SampleSession.cpp`、`test_postprocessor.cpp`、`test_collision.cpp`、`test_main.cpp`、`CMakeLists.txt`。  
4. Opw：`git diff HEAD -- OpwKinematics.*` 空；工作树 md5 = HEAD 对象 md5。  
5. `rg` 确认 `include/olp/algo` + `src/algo` **无** Qt/`QObject`/`QString`。  
6. 深度：逐主链推演 + 点名三项专核；不粘贴协调方证据表当本轮结论。

---

## 主链 8 项 — 现取证行号表

| # | 主链项 | 判定 | 本轮现取证（path:line） |
| :--- | :--- | :--- | :--- |
| 1 | 默认门禁（`blockOnCollision`/`blockOnConfigJump` → validate severities） | **CLOSED/OK** | 默认 true：`FanucLsPostProcessor.h:15-16`；Collision Error/Warning：`FanucLsPostProcessor.cpp:110-120`；ConfigJump：`:123-131`；`result.ok=!hasErrors()`：`:207`；`!ok`→空串：`:215-225`；空 body→`exportToFile` false：`:393-396`；UI 默认勾选：`LsExportDialog.cpp:61-62`；`config_{}` 默认：`LsExportDialog.h:29` |
| 2 | Near 不短路 Hard | **CLOSED/OK** | 拆旗：`CollisionAndSim.cpp:282-284`；扫掠条件仅 `!postedHardSweepCollision`：`:356-357`；Hard 置旗+写 Collision：`:364-373`；Near 另旗不中断：`:374-386`；Test 9b **强制** `sawNear`：`test_collision.cpp:274-276` + Hard/最终 Collision：`:271-278` |
| 3 | Hard 内 Collision 优先（`shouldUpgradeCacheStatus`） | **CLOSED/OK** | 实现：`CollisionAndSim.cpp:37-65`（同 Hard 且 incoming=Collision → true）；公开 API：`CollisionAndSim.h:132-137`；写回调用：`:317-318,:338-339,:372-373,:384-385`；`cache.valid` 故意不改：`:26`；Test 9c：`test_collision.cpp:287-308`；注册：`test_main.cpp:48` |
| 4 | gate-off Collision → Warning（禁静默纯绿） | **CLOSED/OK** | `blockOnCollision=false` → Warning：`FanucLsPostProcessor.cpp:112-120`；UI 有 Warning 走黄列：`LsExportDialog.cpp:143-157`（仅 `ok==true` 且 issues 非空）；锁测：`test_postprocessor.cpp:214-232` |
| 5 | export/refresh ensure；const 缺仿真拒 | **CLOSED/OK** | stale 规则：`MainWindow.cpp:551-564`；ensure：`:566-598`；const validate 拒：`:609-620`；preview 拒：`:631-637`；`exportFanucLs` 先 ensure：`:647-656`；`exportAllPrograms` per-program ensure：`:669-692`；dialog refresh ensure：`LsExportDialog.cpp:120-122`；写盘走 `exportFanucLs`：`:215`；文档变更清旗：`MainWindow.cpp:19` |
| 6 | `utf8ToPath` / 路径 | **CLOSED/OK** | API：`Utf8Path.h:11`；实现 Win/非 Win：`Utf8Path.cpp:19-30`；`exportToFile`/`exportControllerPrograms`：`FanucLsPostProcessor.cpp:399-406,:430-442`；`#include Utf8Path`：同文件头；ProjectSerializer 改调共享：`:803,:872`；diff 删本地匿名 utf8；CMake 入 `olp_io`：`CMakeLists.txt:123` |
| 7 | 水印/红条（RISK-001 draft；不关 kinematics RISK） | **CLOSED/OK（门禁）/ RISK-001 仍 OPEN** | `/MN` 首条：`FanucLsPostProcessor.cpp:240-242`；`LINE_COUNT=mnLines.size()`：`:328`；红条独立局部 QLabel：`LsExportDialog.cpp:32-42`；`refreshPreview` 只改 `statusLabel_`/`codeEdit_`（`:114-178`），**零**引用 `riskBanner`；锁测水印：`test_postprocessor.cpp:395-417` |
| 8 | OpwKinematics 相对 HEAD 零 diff；`olp_algo` 无 Qt | **CLOSED/OK** | `git diff HEAD -- OpwKinematics.*` → 0 bytes；h/cpp md5 与 `git show HEAD:…` 一致；`rg Qt\|QObject\|QString\|Q_` on `src/algo`+`include/olp/algo` → **NO_QT** |

**SampleSession 拒写**：`SampleSession.cpp:218-249` — `!ok \|\| blockingIssue` → 不调用 `exportToFile`，exit 2。  
**端到端中段硬撞**：Test 9 `test_collision.cpp:187-194`（cache Collision + 默认 validate `!ok`）。

---

## 协调方点名重点抽核（专节）

### A) 文档级 freshness（非 per-program）— 协调方 P2-A

**判定：同意 P2-A = OPEN P2（不定 P1）。**

现取证：

- `exportSimEnsured_` 为 **文档级** 单 bool（`MainWindow.h:149-150`）。  
- `exportSimulationMissingOrStale()`：有碰撞几何时，只要 `exportSimEnsured_==true` **或** `lastSimulationResult_.frames` 非空即视为非 stale（`MainWindow.cpp:551-564`）——**不**按 `programId` 记账。  
- 写出路径仍 **per-program** ensure：`exportFanucLs` `:647`；`exportAllPrograms` `:674-686`。  
- 附加测弱：GATE-006 对 PART_B 的「清拒」断言（`test_postprocessor.cpp:592-601`）在「仅 ensure A」后也可因文档级旗变绿，**不能单独**证明 B 被 ensure；产品循环本身（`:674-686`）仍成立。

**严重度**：维持 **P2**。写出主链有 per-program ensure，不定 P1；const validate/preview 多程序可误导。

### B) GATE-006 负路径（ensure 失败跳过 controller）

**判定：同意测弱 = OPEN P2；产品行为正确。**

现取证：

- ensure 失败仅当 `findProgramInDocMutable` 空（`MainWindow.cpp:567-569`）。  
- `exportAllPrograms` 失败 → errors + `continue` 跳过 bulk（`:676-688`）。  
- 正常遍历 `cu.programs` 时，`key=id/name` 几乎总能命中自身 → ensure-fail→skip 分支近死代码。  
- 锁测覆盖：stale 清拒 PART_A/B、空 controllers（`test_postprocessor.cpp:549-607`）；**无**构造 ensure=false→skip-controller 负路径。

**严重度**：维持 **P2**（测覆盖缺口，非门禁击穿）。

### C) SampleSession 拒写不删旧 `WELD_001.ls`

**判定：同意 = OPEN P2。**

现取证：

- 拒写分支：`SampleSession.cpp:228-237` 只打日志，**不** `exportToFile`。  
- 全文件 `rg`：无 `remove`/`unlink`/`filesystem::remove` 针对 `WELD_001.ls`。  
- 成功路径才写：`:238-239`。  
- 残留：磁盘旧 `build/WELD_001.ls` 可「看似正式」。

**严重度**：维持 **P2**。

---

## 与协调方对照（同意 / 不同意）

| 主题 | 协调方 | Audit_X 本轮 | 同意？ |
| :--- | :--- | :--- | :--- |
| 总评口径 | `PASS_WITH_RISKS` | 正式 **APPROVED**；风险分层同意 PASS_WITH_RISKS | **同意风险分层**；Verdict 规则用 APPROVED |
| 无新 P0/P1 | 声称无 | 独立复核：**确认无** | **同意** |
| 主计划 CLOSED | 勿重开除非 P0 | **仍 CLOSED** | **同意** |
| GATE-001..006 主体 | PASS | 主链 8 项均 CLOSED/OK | **同意** |
| OPEN-P1-1..4 已关 | 确认关闭 | sawNear 强制 / ensure / gate-off Warning / Hard Collision 优先 — 现树成立 | **同意** |
| OPEN-P2-GATE005-1 | 产品关闭；负路径测弱 P2 | 同左 | **同意** |
| P2-A 文档级 freshness | OPEN P2 | **同意 OPEN P2**；并指出 PART_B 清拒断言偏弱 | **同意** |
| P2-B GATE-006 负路径 | OPEN P2 | **同意** | **同意** |
| P2-C SampleSession 旧文件 | OPEN P2 | **同意** | **同意** |
| P2-D..I / RISK-001 | 残留 | 抽核水印≠修 Opw；RISK-001 **仍 OPEN** | **同意** |
| Opw 零 diff / olp_algo 无 Qt | PASS | 本轮哈希+rg 再核 | **同意** |
| Windows 805/0 | 采信未复跑 | **同样采信，未复跑** | **同意** |
| 协调方证据行号 | 自称准确 | 本轮重读后与现树一致（容许小幅行漂已按现树重标） | **未见造假** |

**不同意点**：**无实质性不同意**。仅口径：协调方用 `PASS_WITH_RISKS` 强调风险残留；本任务规则下无新 P0/P1 → 正式写 **APPROVED**，并明确同意其风险分层。

---

## 新问题

| 级别 | ID | 说明 |
| :--- | :--- | :--- |
| P0 | （无） | 默认 `LsPostConfig{}` + ensure 写出路径未见硬撞/ConfigJump 静默可导出 |
| P1 | （无） | 先前 OPEN-P1-1..4 / OPEN-P2-GATE005-1 关闭证据在现树成立 |
| P2 | P2-A..I（既有） | 与协调方列表一致；本轮未新开 P2 ID。补充：GATE-006 PART_B 清拒断言对「逐 program ensure」证明力偏弱（归入 P2-A/B，不升 P1） |

---

## 设计对齐（v1.1 摘录）

- M12 后处理校验/预览/`.ls` + ROBOGUIDE 验收 → 与默认门禁、水印、红条一致；RISK-001 草稿门禁符合「未金样对拍不得当真机」。  
- M10 离散+段内采样、碰撞/接近两级 → Near/Hard 拆分与写回一致。  
- 「算法层不依赖 Qt」→ `CollisionAndSim` / Opw 无 Qt；ensure 在 UI `MainWindow`。  
- J2/J3 需 ROBOGUIDE 核对 → 计划冻结 Opw，仅水印 — 符合。

---

## 未检查

- box 未编译、未跑 ctest；**未复验** 805/0。  
- 未 GUI 点击对话框；未打开真实样件 `.ls`。  
- 未跑 ROBOGUIDE 金样（RISK-001 OPEN）。  
- 未审 HTML 导出器、CommandManager 大拆、cadHandle/dirty（计划冻结）。  
- 未验证极稀 `sampleStepSec` 漏撞、Ignored 角色误标等预存风险的运行时几何。

---

## Bottom line（可转发 Verdict 包）

- **Verdict: APPROVED**  
- **同意协调方 PASS_WITH_RISKS：是** — 默认主链安全拒止成立，但 P2+RISK-001+未复跑 805/0，不定零风险。  
- **主计划：仍 CLOSED**  
- **新 P0/P1：无**

*End of FULL REAUDIT Audit_X.*

---

# 审核结论 — FIX-EXPORT-GATE-006 Round 1

> **Task ID**：`FIX-EXPORT-GATE-006`
> **Task**：消化 OPEN-P2-GATE005-1（`exportAllPrograms` 导出前 ensure）
> **Round**：1
> **Reviewer**：Si_FastSuite_Audit_X（独立交叉审核执行端）
> **Reviewed at**：2026-10-29 ~22:02 Asia/Singapore (UTC+8)
> **Executor**：执行方（Si_FastSuite；本轮仅审，未改工程业务/测试源码）
> **Verdict**：**APPROVED**
>
> **方法**：`git fetch/status/pull`（working tree 相对 `origin/main`）；通读 `CRDocs/STATUS.md`、`IMPLEMENTATION.md`、本文件 GATE-005 残留、`CROSS_AUDIT_FINAL_COORDINATOR.md` OPEN-P1-2/`OPEN-P2-GATE005-1` 描述；`rg`+`Read` 取证 `exportAllPrograms` / `ensureProgramSimulatedForExport` / GATE-006 锁测；核对 `exportFanucLs`/`LsExportDialog` 既有门禁未削弱；冻结核对 `OpwKinematics` 哈希=HEAD、`olp_algo` 无 Qt。
>
> **未改**工程业务/测试源码；**禁止 CloudAgent**；仅更新本文件与 `CRDocs/STATUS.md`。
> **Windows 测试**：采信协调方/执行方声称 — 2026-10-29 Asia/Singapore，`Compile.bat` exit 0，`SiFastSuiteOLP_AllTests` **805 passed / 0 failed**（含 GATE-006）。**Audit_X 本端未复跑**（box 无完整 Qt/OCCT/FCL Windows 工具链）。

## Verdict

**APPROVED**。`OPEN-P2-GATE005-1` **CLOSED**。未见新 P0/P1。**勿重开** `PlanDocs/FIX-EXPORT-SAFETY`（仍 CLOSED；本项为已关闭计划后的小额 P2 follow-up）。

| ID | 判定 | 一行证据摘要 |
|---|---|---|
| OPEN-P2-GATE005-1 | **CLOSED** | `MainWindow.cpp:669-692` per-program ensure → 失败记错+`continue` 跳过 bulk；锁测 `test_postprocessor.cpp:549-607` |

---

## 范围

对照协调方摘要 + GATE-005 REVIEW 残留 + IMPLEMENTATION：

1. `exportAllPrograms` 在 `exportControllerPrograms` 前对每个 program 调用 `ensureProgramSimulatedForExport`（id 优先，否则 name）
2. ensure 失败 → 收集 errors，跳过该 controller 的 bulk 写出
3. 单测 GATE-006（stale 清拒 PART_A/PART_B + 空 controllers 拒）
4. 不削弱既有单程序 `exportFanucLs` / const validate-preview 门禁

冻结：`OpwKinematics.{h,cpp}` 工作树哈希 = HEAD（`e02edf1…` / `73b34cc…`）；`include/olp/algo` + `src/algo` **无 Qt include**。

---

## 逐条取证

### OPEN-P2-GATE005-1 — `exportAllPrograms` ensure — **CLOSED**

**GATE-005 残留要点**：`exportAllPrograms` 曾直接 `exportControllerPrograms`，不走 ensure（约旧行 659-674）；全库原无调用方。

**实现**：

| 证据 | 说明 |
|---|---|
| `src/ui/MainWindow.cpp:659-667` | 空 controllers → false + `"Document has no controllers"`（行为保留） |
| `src/ui/MainWindow.cpp:669-686` | 每 controller：遍历 `cu.programs`；`key = id 非空 ? id : name`；`ensureProgramSimulatedForExport(key)`；失败 → `controllerEnsureOk=false`、`allSuccess=false`、errors 推 `"No program found for export (ensure failed): <label>"` |
| `src/ui/MainWindow.cpp:687-688` | `!controllerEnsureOk` → **`continue`**，不调用 `exportControllerPrograms` |
| `src/ui/MainWindow.cpp:690-692` | 全部 ensure 成功才 bulk；bulk 失败仍累计 `allSuccess` |
| `src/ui/MainWindow.cpp:566-598` | ensure 本体未改（GATE-005）：无几何标 ensured；有几何则 `validateAndSimulateProgram` 写回 cache |
| `src/ui/MainWindow.cpp:642-656` | `exportFanucLs` 仍先 ensure — **未削弱** |
| `src/ui/MainWindow.cpp:601-639` | const validate/preview stale 拒仍在 |
| `src/ui/LsExportDialog.cpp:122,215` | 预览 ensure + 写盘走 `exportFanucLs` — **未削弱** |

**测试**：

| 证据 | 说明 |
|---|---|
| `tests/test_postprocessor.cpp:463-470` | fixture：`PART_A`/`PART_B`（id 空 → key=name，ensure 可分程命中） |
| `tests/test_postprocessor.cpp:549-571` | GATE-006：有 Fixture + 空 frames → 调用前 const 拒 |
| `tests/test_postprocessor.cpp:578-590` | `exportAllPrograms` 后 PART_A 不再 `"no fresh simulation"` |
| `tests/test_postprocessor.cpp:592-601` | PART_B 同样清拒（覆盖多 program） |
| `tests/test_postprocessor.cpp:603-607` | 空 controllers → `!ok` 且 errors 非空 |
| `tests/test_postprocessor.cpp:504-546` | GATE-005 单程序 stale/ensure 锁测 **仍在** |

**判断**：CLOSED。每个 program 均 ensure；失败路径记错并跳过该 controller；单程序门禁未削弱；无静默纯绿新路径（ensure 成功后仍经 `exportToFile`/`validateProgram` 碰撞门禁，与 `exportFanucLs` 同级）。

**测弱（P2，不升 P1）**：未强制构造 ensure 返回 false 的用例（ensure 仅「找不到 program」返 false；正常遍历 `cu.programs` 几乎总成功）。失败跳过路径靠代码审读确认，不挡 CLOSED。

---

## 连带 / 新问题

| 级别 | ID | 说明 |
|---|---|---|
| （无 P0） | — | 未见击穿默认碰撞/导出门禁 |
| （无新 P1） | — | — |
| P2（测弱） | — | GATE-006 未锁 ensure-fail→skip-controller 负路径 |
| P2（既有设计） | — | `exportSimEnsured_` 文档级标志；多 program 覆盖 `lastSimulationResult_` — GATE-005 既有，非本轮新引入 |
| P2（既有） | OPEN-P2-1..6 | 交叉审计其他项；本轮非目标 |
| 说明 | — | `PlanDocs/FIX-EXPORT-SAFETY-PLAN.md` 保持 **CLOSED**；主线 `RISK-001` 等仍 OPEN |

---

## 证据索引（关键路径:行号）

1. `src/ui/MainWindow.cpp:674-688` — per-program ensure + 失败跳过 controller
2. `src/ui/MainWindow.cpp:690` — 成功后才 `exportControllerPrograms`
3. `src/ui/MainWindow.cpp:647` — 单程序 export 仍 ensure
4. `tests/test_postprocessor.cpp:549-607` — GATE-006 锁测
5. `tests/test_postprocessor.cpp:504-546` — GATE-005 锁测仍在
6. Opw 哈希 = HEAD；algo 无 Qt include

---

## 结论（给协调方）

- **Verdict = APPROVED**
- **OPEN-P2-GATE005-1 = CLOSED**
- **新 P0/P1：无**
- **主计划 `FIX-EXPORT-SAFETY`：仍 CLOSED**（勿重开，除非发现 P0）
- Windows：**采信协调方 805 passed / 0 failed**；**Audit_X 未复跑**

---

# 审核结论 — FIX-EXPORT-GATE-005 Round 1

> **Task ID**：`FIX-EXPORT-GATE-005`
> **Task**：交叉审计 OPEN-P1-1..4 关闭（P1 closeout after FINAL）
> **Round**：1
> **Reviewer**：Si_FastSuite_Audit_X（独立交叉审核执行端）
> **Reviewed at**：2026-10-29 ~21:55 Asia/Singapore (UTC+8)
> **Executor**：执行方（Si_FastSuite；本轮仅审，未改工程业务/测试源码）
> **Verdict**：**APPROVED**
>
> **方法**：`git fetch/status`（working tree 相对 `origin/main`）；通读 `CRDocs/CROSS_AUDIT_FINAL_COORDINATOR.md` §3、`IMPLEMENTATION.md`、`STATUS.md`、`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md`；对 OPEN-P1-1..4 **逐条** `rg`+`Read` 取证（CollisionAndSim / FanucLsPostProcessor / MainWindow / LsExportDialog / test_collision / test_postprocessor / test_main / test_support）；核对 `shouldUpgradeCacheStatus` 为 public、`OpwKinematics` 相对 HEAD 零 diff、`olp_algo` 无 Qt include。
>
> **未改**工程业务/测试源码；**禁止 CloudAgent**；仅更新本文件与 `CRDocs/STATUS.md`。
> **Windows 测试**：采信协调方/执行方声称 — 2026-10-29 ~21:53 Asia/Singapore，`Compile.bat` exit 0（首轮 C2248 private 已改 public），`SiFastSuiteOLP_AllTests` **800 passed / 0 failed**。**Audit_X 本端未复跑**（box 无完整 Qt/OCCT/FCL Windows 工具链；仓库无 build 产物）。

## Verdict

**APPROVED**。OPEN-P1-1..4 **均 CLOSED**；未见新 P0/P1。**计划关闭阻塞解除**（可按「本计划范围无 OPEN P0/P1」口径关闭 `FIX-EXPORT-SAFETY`；主线 `RISK-001` 等仍 OPEN）。

| ID | 判定 | 一行证据摘要 |
|---|---|---|
| OPEN-P1-1 | **CLOSED** | `tests/test_collision.cpp:274-276` `check(sawNear, …GATE-005…)` |
| OPEN-P1-2 | **CLOSED** | `MainWindow.cpp:647` / `LsExportDialog.cpp:122` ensure；const 拒 `MainWindow.cpp:610-620,631-637`；锁测 `test_postprocessor.cpp:504-546` |
| OPEN-P1-3 | **CLOSED** | `FanucLsPostProcessor.cpp:110-120` 始终报告；gate on→Error / off→Warning；锁测 `test_postprocessor.cpp:214-232` |
| OPEN-P1-4 | **CLOSED** | `CollisionAndSim.h:135-137` public；`CollisionAndSim.cpp:37-65` Hard 内 Collision 优先；Test 9c `test_collision.cpp:287-308` + `test_main.cpp:48` |

---

## 范围

对照 `CROSS_AUDIT_FINAL_COORDINATOR.md` §3 OPEN-P1-1..4 关闭意图 + 本轮协调方摘要：

1. Test 9b 强制 `sawNear`
2. 有碰撞几何时 export/refresh → `ensureProgramSimulatedForExport`；const validate/preview 缺仿真 → 明确 Error 拒
3. Collision 始终入 issues；gate on→Error，gate off→Warning（禁静默纯绿）
4. `shouldUpgradeCacheStatus` public；Hard 内 Collision 可替换 ConfigJump/Unreachable/JLE；Test 9c

冻结核对：`OpwKinematics.{h,cpp}` 工作树哈希 = HEAD（`e02edf1…` / `73b34cc…`）；`rg` 确认 `include/olp/algo` + `src/algo` **无 Qt include**。

---

## 逐条取证（对照 §3）

### OPEN-P1-1 — Test 9b `sawNear` — **CLOSED**

**§3 要点**：Test 9b 不断言 `sawNear==true`，失败信息仅打印 `nearIssueSeen`；计划项 2「先 Near 再硬撞」测弱。关闭：加 `check(sawNear, …)`。

**实现/测试**：

| 证据 | 说明 |
|---|---|
| `tests/test_collision.cpp:258-268` | 遍历 `sim.issues` 置 `sawNear` / `sawHard` |
| `tests/test_collision.cpp:274-276` | **新增** `check(sawNear, "FIX-EXPORT-GATE-005: NearCollision must be observed before hard Collision …")` |
| `tests/test_collision.cpp:271-283` | 仍锁 `sawHard`、最终 `cache.status==Collision`、`cache.valid` |
| `tests/test_main.cpp:47` | 仍注册 `testNearSweepDoesNotShortCircuitHardCollision` |

**判断**：CLOSED。断言强制观察到 Near；若 Near 被短路则红。注：断言不比较 issues 时间序，但与「Near 不短路 Hard」语义等价锁定。

### OPEN-P1-2 — 导出 freshness — **CLOSED**

**§3 要点**：`exportFanucLs` / `LsExportDialog` / `exportAllPrograms` 不强制重跑仿真，门禁盲信 `target.cache`。处置：文档化已知限制 **或** 导出前强制/提示重仿真。

**实现/测试**：

| 证据 | 说明 |
|---|---|
| `include/olp/ui/MainWindow.h:90-93,128-130,149-150` | 声明 `ensureProgramSimulatedForExport` / freshness 辅助 / `exportSimEnsured_` |
| `src/ui/MainWindow.cpp:530-548` | `documentHasCollisionRelevantGeometry`（非 Ignored + AABB/角色） |
| `src/ui/MainWindow.cpp:551-564` | `exportSimulationMissingOrStale`：有碰撞几何且无 frames 且未 ensure → stale |
| `src/ui/MainWindow.cpp:566-598` | `ensureProgramSimulatedForExport` 对当前场景重跑 `validateAndSimulateProgram` |
| `src/ui/MainWindow.cpp:601-622` | const `validateProgramForExport`：stale → Error「no fresh simulation」 |
| `src/ui/MainWindow.cpp:625-639` | const `previewFanucLs`：stale → 空串 + 明确 errorOut |
| `src/ui/MainWindow.cpp:642-656` | `exportFanucLs` **先** `ensureProgramSimulatedForExport` |
| `src/ui/LsExportDialog.cpp:120-122` | `refreshPreview` 开头 ensure |
| `src/ui/LsExportDialog.cpp:215` | 对话框写盘走 `exportFanucLs`（再 ensure） |
| `src/ui/MainWindow.cpp:19` | `handleDocumentChanged` 清 `exportSimEnsured_=false` |
| `tests/test_postprocessor.cpp:504-546` | 有 Fixture + 空 frames → const 拒；ensure 后清除 stale 拒信文案 |

**判断**：CLOSED（相对本轮关闭摘要与 §3 处置选项）。主 UI/API 导出与预览刷新已强制重仿真；const 路径明确拒信。

**残留（P2，不升 P1）**：`MainWindow::exportAllPrograms`（`src/ui/MainWindow.cpp:659-674`）仍直接 `exportControllerPrograms`，**未** ensure。全库无调用方（仅声明/定义）。记 `OPEN-P2-GATE005-1`；不挡计划关闭。

### OPEN-P1-3 — gate off 禁纯绿静默 — **CLOSED**

**§3 要点**：`blockOnCollision=false` 时 Collision 完全不报告 → 纯绿可导出硬撞。关闭：卸载门禁至少 Warning 黄列。

**实现/测试**：

| 证据 | 说明 |
|---|---|
| `src/io/FanucLsPostProcessor.cpp:110-120` | Collision **始终** `push_back`；`blockOnCollision? Error : Warning` |
| `src/io/FanucLsPostProcessor.cpp:207` | `result.ok = !hasErrors()` — Warning 不挡 `ok` |
| `src/ui/LsExportDialog.cpp:143-157` | `ok && !issues.empty()` → 黄「校验通过（含警告）」 |
| `tests/test_postprocessor.cpp:214-232` | gate off：`ok`、无 Error、issues 非空、含「hard collision」Warning；`generateLsString` 非空 |
| `tests/test_postprocessor.cpp:234-241` | 默认 gate 仍拒 Collision（未削弱项 1） |

**判断**：CLOSED。无「静默纯绿」路径；门禁语义未反转。

### OPEN-P1-4 — Hard 同级 Collision 优先 — **CLOSED**

**§3 要点**：同级 Hard 严格 `>`，先 ConfigJump 再 Collision 不升级；`blockOnConfigJump=false` 且 `blockOnCollision=true` 时硬撞可被挡住。关闭：Collision 优先或单测锁顺序。

**实现/测试**：

| 证据 | 说明 |
|---|---|
| `include/olp/algo/CollisionAndSim.h:132-137` | `shouldUpgradeCacheStatus` **public static**（修 C2248） |
| `src/algo/CollisionAndSim.cpp:19-27` | `upgradeTargetCacheFromSegmentIssue` 改调该策略 |
| `src/algo/CollisionAndSim.cpp:37-65` | Soft < Near < Hard；同级 Hard 且 `incoming==Collision && current!=Collision` → 升级 |
| `tests/test_collision.cpp:287-308` | Test 9c 真值表：ConfigJump/Unreachable/JLE→Collision；禁降级 |
| `tests/test_support.h:22` + `tests/test_main.cpp:48` | 声明并注册 |

**判断**：CLOSED。策略与单测锁定；public 可访问。注：9c 为策略真值表（非活扫掠集成），符合 §3「至少单测锁该顺序」。

---

## 连带 / 新问题

| 级别 | ID | 说明 |
|---|---|---|
| （无 P0） | — | 默认门禁主链未见击穿 |
| （无新 P1） | — | — |
| P2 | **OPEN-P2-GATE005-1** | `exportAllPrograms` 不走 ensure（无调用方） |
| P2 | （既有） | 交叉审计 OPEN-P2-1..6；本轮非目标 |
| 说明 | — | `ensure`/`validateOperation` 写 cache 不经 CommandManager — 与既有 `validateOperation` 模式一致，非本轮新引入 P1 |
| 说明 | — | `olp_algo` 无 Qt；UI 导出仍经后处理 +（对话框）CommandManager 文档变更路径清 `exportSimEnsured_` |

---

## 证据索引（关键路径:行号）

1. `tests/test_collision.cpp:274-276` — P1-1 `check(sawNear)`
2. `src/ui/MainWindow.cpp:647` + `src/ui/LsExportDialog.cpp:122` — P1-2 ensure
3. `src/ui/MainWindow.cpp:610-620,631-637` — P1-2 const 拒
4. `tests/test_postprocessor.cpp:504-546` — P1-2 锁测
5. `src/io/FanucLsPostProcessor.cpp:110-120` — P1-3 分流
6. `tests/test_postprocessor.cpp:214-232` — P1-3 锁测
7. `include/olp/algo/CollisionAndSim.h:135-137` + `src/algo/CollisionAndSim.cpp:37-65` — P1-4 策略
8. `tests/test_collision.cpp:287-308` — P1-4 Test 9c
9. Opw 哈希 = HEAD；algo 无 Qt include

---

## 结论（给协调方）

- **Verdict = APPROVED**
- OPEN-P1-1..4 **全部 CLOSED**
- **计划关闭阻塞：解除**（`FIX-EXPORT-SAFETY` 可按完成定义关闭）
- 主线仍 OPEN：`RISK-001`、`RISK-002`、`M10-FCL-005`、`M11-009`、`M14-012`、`M14-013`、`M15-001`；另记 `OPEN-P2-GATE005-1`
- Windows：**协调方声称 800 passed / 0 failed**；**Audit_X 未复跑**

---

# 审核结论 — FIX-EXPORT-GATE-FINAL（全量独立深度审查）

> **Task ID**：`FIX-EXPORT-GATE-FINAL`
> **Task**：对 `PlanDocs/FIX-EXPORT-SAFETY-PLAN.md` 项 1–4 **累计 diff** 全量独立深度审查（交互与连带回归）
> **Round**：FINAL
> **Reviewer**：Si_FastSuite_Audit_X（独立审核执行端）
> **Reviewed at**：2026-10-29 Asia/Singapore (UTC+8)
> **Executor**：执行方（Si_FastSuite；本轮仅审）
> **Verdict**：**APPROVED**
>
> **方法（本轮重新取证，禁止复用各 Round 旧 REVIEW 结论作证据）**：
> 1. `git status` / `git diff HEAD --stat` / 全量相关 `git diff HEAD`；
> 2. 通读未跟踪文件 `include/olp/io/Utf8Path.h`、`src/io/Utf8Path.cpp`、`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md`；
> 3. 精读实际源码：`FanucLsPostProcessor.{h,cpp}`、`CollisionAndSim.cpp`（扫掠与写回）、`LsExportDialog.{h,cpp}`、`SampleSession.cpp`、`ProjectSerializer.cpp`（utf8 抽离）、`CMakeLists.txt`、`tests/test_{postprocessor,collision,main,support.*}`；
> 4. 交叉核对 `MainWindow::{previewFanucLs,exportFanucLs,exportAllPrograms}` 调用链；
> 5. `git hash-object` 证明 `OpwKinematics.{h,cpp}` 与 `HEAD` 字节一致；确认根 `Plan.md` / `.claude/Plan.md` / `PlanDocs/STATUS.md`(IDLE) / DIR-001 **无 diff**。
>
> **未改**工程业务/测试源码；**禁止 CloudAgent**；**仅**更新本文件与 `CRDocs/STATUS.md`。
> **Windows 采信**（IMPLEMENTATION §四 / 项4 记录）：2026-10-29 ~20:56 Asia/Singapore，`Tools\Compile.bat` exit 0；`SiFastSuiteOLP_AllTests` **783 passed / 0 failed**（含 GATE-001..004）。**本审核 box 未复跑**编译/测试（无完整 Qt/OCCT/FCL Windows 工具链）。

## 总判定

| 范围 | 结论 |
|---|---|
| 项 1 门禁 | **PASS** |
| 项 2 扫掠写回 | **PASS**（含「端点 Valid、段中撞 → 默认不漏拦」证明） |
| 项 3 utf8 + 锁测 | **PASS** |
| 项 4 水印/横幅 + Opw 冻结 | **PASS**；**RISK-001 仍须 OPEN** |
| 交互与连带清单（7 条） | **主链 PASS**；协调方另列 OPEN-P1-1..4（见补记） |
| 计划「完成定义」 | **主链达标；关闭阻塞** — 协调方 OPEN-P1-1..4 未清；主线 DIR-001 / RISK-001 仍 OPEN |

**Verdict = APPROVED（主链）**。

> **2026-10-29 补记（吸收协调方交叉抽核，非复审改码）**：协调方独立交叉抽核见 `CRDocs/CROSS_AUDIT_FINAL_COORDINATOR.md`，Verdict=**PASS_WITH_RISKS**。**不同意**上文「本计划范围内无 OPEN P0/P1 → 可关闭本修复计划」的完成定义口径。Audit_X **保留**项 1–4 主链 APPROVED（默认仿真→validate→拒写 + utf8 + 水印主体达标），但承认 FINAL 对连带脚枪/测弱项偏乐观。
>
> **计划关闭阻塞于协调方 OPEN P1**（详情与证据以交叉抽核文档为准）：
> - **OPEN-P1-1**：Test 9b 不断言 `sawNear==true`
> - **OPEN-P1-2**：UI/MainWindow 导出不强制重跑 `validateAndSimulateProgram`
> - **OPEN-P1-3**：卸载碰撞门禁后 Collision 不报告，可纯绿导出硬撞
> - **OPEN-P1-4**：Hard 同级严格 `>`，ConfigJump 可挡住后续 Collision 状态文案/升级
>
> 在 P1 修复轮再审通过或书面降级签收前，**不得**按计划完成定义关闭 FIX-EXPORT-SAFETY。`STATUS.md` 已同步为「主链 APPROVED / 计划关闭阻塞」。本补记未改工程代码。

---

## 累计改动面核对（本轮）

**已跟踪相对 HEAD（业务/测）**：`CMakeLists.txt`、`FanucLsPostProcessor.h/cpp`、`CollisionAndSim.cpp`、`ProjectSerializer.cpp`、`LsExportDialog.cpp`、`SampleSession.cpp`、`test_collision.cpp`、`test_postprocessor.cpp`、`test_main.cpp`、`test_support.h`，以及 `CRDocs/*`。

**未跟踪（必须纳入，已审）**：`include/olp/io/Utf8Path.h`、`src/io/Utf8Path.cpp`、`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md`。

**明确未改**：`OpwKinematics.*`（工作树哈希 = HEAD：`73b34cc…` / `e02edf1…`）；根 `Plan.md`、`.claude/Plan.md`、DIR-001、`PlanDocs/STATUS.md`(IDLE)。

---

## 项 1 — 后处理默认门禁 — PASS

| 检查点 | 结论 | 本轮证据 |
|---|---|---|
| `blockOnCollision` / `blockOnConfigJump` 默认 true | **PASS** | `include/olp/io/FanucLsPostProcessor.h:15-16` |
| `validateProgram`：Collision / ConfigJump → Error；Near / Singularity → Warning | **PASS** | `src/io/FanucLsPostProcessor.cpp:110-146`；`result.ok=!hasErrors()` 于 `:203`；`hasErrors` 定义于头文件 `:36-41` |
| `LsExportDialog` 默认勾选碰撞阻断；ok+Warning 黄列；ConfigJump 保持默认 | **PASS** | `LsExportDialog.cpp:61-62` `setChecked(true)`；`:139-160` 有 issue 时黄「校验通过（含警告）」否则绿；成员 `config_{}`（`LsExportDialog.h:29`）默认构造保留 `blockOnConfigJump=true`，`refreshPreview:115-118` **不**覆写该字段 |
| `SampleSession` 默认拒写正式 `WELD_001.ls` | **PASS** | `SampleSession.cpp:218-250`：`refuseProductionLs = !lsVal.ok \|\| blockingIssue`；真则不调用 `exportToFile`，`exitCode=2`，summary 明示门禁拒绝 |

锁测（本轮源码）：`tests/test_postprocessor.cpp` GATE-001 默认拒 Collision/ConfigJump、Near/Singularity 可导出 Warning（约 `:214-246`）。

---

## 项 2 — 段内扫掠短路与 cache 回写 — PASS

| 检查点 | 结论 | 本轮证据 |
|---|---|---|
| Near 不短路硬撞；`sweepCollided`→Collision | **PASS** | `CollisionAndSim.cpp:274-276` 拆 `postedHardSweepCollision` / `postedNearSweepIssue`；`:349` 仅 hard 短路；`:356-365` `sweepCollided` 强制 Collision + 写回；`:366-378` Near 单独、可继续扫 |
| 段内问题按优先级回写 `cache.status`；不改 `cache.valid` | **PASS** | `:16-49` `CacheSeverityTier` Hard>Near>Soft；`upgradeTargetCacheFromSegmentIssue` 仅升 status/message，注释明示不动 `valid`；调用点 Unreachable `:309-310`、ConfigJump `:330-331`、Collision `:364-365`、Near `:376-377` |
| **端点 Valid、段中撞 → 默认 validateProgram 不漏拦** | **PASS（已证明）** | 因果链：① 端点先置 Valid（`:250-251`，`valid=true` 于 `:231`）；② 段中 `sweepCollided` 写回**该段目标** `target.cache.status=Collision`（`:364-365`），`valid` 仍 true；③ `validateProgram` 遍历**所有** target（`:60` 起），`blockOnCollision && status==Collision` → Error（`:110-116`）→ `ok=false`；④ `generateLsString` 拒空串（`:211-221`）。单测锁：`test_collision.cpp:182-194`（写回 Collision + `validateProgram !ok`）；`test_postprocessor.cpp` GATE-003 live sweep 块（约 `:286-354`） |

Near→Hard 升级单测：`testNearSweepDoesNotShortCircuitHardCollision`（`test_collision.cpp:199-280`，经 `test_main.cpp` 注册）。

---

## 项 3 — utf8 共享与门禁锁测 — PASS

| 检查点 | 结论 | 本轮证据 |
|---|---|---|
| 共享 `utf8ToPath` | **PASS** | 新 `Utf8Path.h/.cpp`（Win `MultiByteToWideChar(CP_UTF8)` / 非 Win `u8path`）；`CMakeLists.txt` 入 `olp_io`；`ProjectSerializer.cpp` 删本地实现、改 `#include "olp/io/Utf8Path.h"`，仍于 `namespace olp::io` 调 `utf8ToPath`（约 `:803,:872`）；`FanucLsPostProcessor.cpp:395-402` `exportToFile` 用 path 打开；`:427-438` `exportControllerPrograms` 用 utf8 字符串拼接路径避免 ACP `path::string()` 回转 |
| 中文路径单测；门禁锁测仍在 | **PASS** | `test_postprocessor.cpp:358-379` 中文目录导出 + 读回 `/PROG`；同文件 GATE-001/003 锁测仍在（端点 Collision、ConfigJump、写回 shape、live sweep） |

---

## 项 4 — RISK-001 草稿门禁 — PASS（RISK 仍 OPEN）

| 检查点 | 结论 | 本轮证据 |
|---|---|---|
| `/MN` 水印；`LINE_COUNT` | **PASS** | `FanucLsPostProcessor.cpp:236-237` 首条 `addMnLine`；`:324-325` `LINE_COUNT`/`MEMORY_SIZE` 基于 `mnLines.size()`；`:337-340` 编号循环 |
| 红条常驻 vs `refreshPreview` | **PASS** | `LsExportDialog.cpp:32-42` 独立 `riskBanner`；`:114-174` 只改 `statusLabel_`/`codeEdit_`，零引用横幅 |
| 未改 OpwKinematics | **PASS** | 工作树无 Opw diff；哈希=HEAD |
| **RISK-001 仍须 OPEN** | **PASS（未误关）** | `CRDocs/STATUS.md` Open 列表含 `RISK-001`；水印文案 `UNVERIFIED`；横幅「严禁…成品下发」；代码无「已对拍/已关闭 RISK」伪装 |

GATE-004 三断言：`test_postprocessor.cpp:375-376,391-397`。

---

## 交互与连带（主动查找）— 全部 PASS

| # | 问题 | 结论 | 本轮证明要点 |
|---|---|---|---|
| 1 | 端点 Valid 段中撞是否漏拦 | **不漏** | 见项 2 因果链 + collision/postprocessor 锁测 |
| 2 | Near 短路是否复现 | **不复现** | hard/near 分旗；9b 单测要求最终 Collision |
| 3 | `cache.valid` 语义是否被破坏 | **未破坏** | 写回故意不改 `valid`；单测要求 Collision 时 `valid==true` |
| 4 | 默认门禁与 UI 一致性 | **一致** | 头文件默认 true；对话框勾选碰撞；ConfigJump 无 UI 开关但 `config_{}` 保持默认 true |
| 5 | 中文路径导出含水印且门禁仍生效 | **是** | utf8 写出 + GATE-004 中文路径水印断言；门禁在 `generateLsString` 先于写盘 |
| 6 | SampleSession 拒写/放行 | **正确** | 拒：不写正式 ls、exit 2；放行：`exportToFile`→含水印 |
| 7 | RISK-001 是否被误关 | **未误关** | STATUS Open + 文案 UNVERIFIED；Opw 未改 |

**计划完成定义**：本计划范围内 **无 OPEN P0/P1** → 可关闭本修复计划。主线 `RISK-001` 等仍 OPEN（计划明示水印≠关 RISK）。

---

## 残留风险（不挡 APPROVED / 不挡关闭本计划）

1. **RISK-001 主线仍 OPEN**（运动学符号/耦合未验证；需 ROBOGUIDE/示教器金样对拍）。
2. Hard 同档（Unreachable/Collision/ConfigJump）互不覆盖 message（`>` 非 `>=`）——均仍为后处理 Error，**不构成漏拦**（P2 信息粒度）。
3. `exportControllerPrograms` / `MainWindow` 路径未再单独 `find` 水印字符串（逻辑必经 `generateLsString`，P2 覆盖）。
4. **box 未复跑**；采信 Windows **783/0**。
5. 无 Warning 时绿「校验通过」与红条并存——分层提示，非清横幅缺陷。

---

## 给协调方 / 主代理（可立刻 priority）

| 字段 | 值 |
|---|---|
| Verdict | **APPROVED** |
| 可否关闭本修复计划 | **可以关闭** `FIX-EXPORT-SAFETY`（项1–4+FINAL 完成定义满足） |
| OPEN P0/P1（本计划范围） | **无** |
| 主线仍 OPEN | `RISK-001`（及 STATUS 既有 RISK-002、M10-FCL-005、M11-009、M14-012/013、M15-001） |
| 项1–4 摘要 | 门禁默认真拒撞/跳变；扫掠 Near 不短路且写回；utf8 共享+中文测；水印+红条；Opw 冻结 |
| 关键证据（≥8） | `FanucLsPostProcessor.h:15-16`；`FanucLsPostProcessor.cpp:110-146`；`:203`；`:236-237`；`:324-325`；`:395-402`；`CollisionAndSim.cpp:40-49`；`:349-378`；`LsExportDialog.cpp:32-42,61-62,139-160`；`SampleSession.cpp:218-250`；`Utf8Path.cpp`+`CMakeLists.txt`；`test_collision.cpp:182-194,272-280`；Opw 哈希=HEAD |
| Windows | **783 passed / 0 failed**（采信；box 未复跑） |

---

# 审核结论 — FIX-EXPORT-GATE-004 Round 1

> **Task ID**：`FIX-EXPORT-GATE-004`
> **Task**：RISK-001 草稿门禁（P0-Gate，不动算法）
> **Round**：1
> **Reviewer**：Si_FastSuite_Audit_X（独立审核执行端）
> **Reviewed at**：2026-10-29 Asia/Singapore (UTC+8)
> **Executor**：执行方（Si_FastSuite）
> **Verdict**：**APPROVED**
>
> 本轮为**格外严格**的独立深度审核（回应用户对前几轮「是否认真核对每个代码、深度思考连带问题」的质疑）：逐段通读 `git diff` 中本项新增点与调用链；精读 `src/io/FanucLsPostProcessor.cpp` `/MN` 组装、编号循环、`LINE_COUNT`/`MEMORY_SIZE`、`exportToFile`、`exportControllerPrograms`；精读 `src/ui/LsExportDialog.cpp` `setupUi` 红条与 `refreshPreview` 全部分支；精读 `tests/test_postprocessor.cpp` 三条 `FIX-EXPORT-GATE-004` 断言及相邻 GATE-001/003 锁测；核对 `SampleSession.cpp` 写出路径、`MainWindow::{previewFanucLs,exportFanucLs,exportAllPrograms}` 是否均经 `generateLsString`；用 `git hash-object` 与 `HEAD` 比对证明 `OpwKinematics.{h,cpp}` **字节级未改**；确认项 1–3 默认门禁 / 扫掠写回 / `utf8ToPath` 锁测仍在。对照权威：`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md` **仅项 4**、`CRDocs/IMPLEMENTATION.md`、`STATUS.md`（审前 READY_FOR_REVIEW）、`Docs/OLP_System_Design_Claude.md` v1.1。**未改**工程业务/测试源码；**禁止 CloudAgent**；**未开** `FIX-EXPORT-GATE-FINAL`；仅更新本文件与 `STATUS.md`。Windows 实测采信用户机（IMPLEMENTATION §四：~20:56 Asia/Singapore 2026-10-29，`Compile.bat` exit 0，**783 passed / 0 failed**，GATE-004 三断言 PASS，`build/WELD_001.ls` 含水印）；**本审核未在 box 复跑编译/测试**（box 无完整 Qt/OCCT/FCL Windows 工具链；仓库内亦无 `WELD_001.ls` 样件文件可本地打开）。

## 总判定

| 项 | 结论 |
|---|---|
| 1. `.ls` `/MN` 头部插入 `! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001)` | **PASS** |
| 2. `LsExportDialog` 顶部常驻醒目提示（未对拍 / 严禁成品下发真机） | **PASS** |
| 3. 单测三断言：`generateLsString` / `exportToFile`（含中文路径）含水印 + 位于 `/MN` 之后 | **PASS** |
| 4. **禁止**修改 OpwKinematics 耦合/turns/offsets/signCorrections | **PASS**（工作树相对 HEAD 哈希一致） |

四条强制范围全部实质达标，与计划项 4 及 `IMPLEMENTATION.md` 声明一致，未见夸大。强制连带排查 A–F **均无发现 P0/P1 连带缺陷**。**Verdict = APPROVED**。**可以**由协调方开 `FIX-EXPORT-GATE-FINAL`（本审核端不提前开 FINAL）。

---

## 逐段 diff / 源码核对（重点文件:行号）

### 1) `/MN` 草稿水印 — PASS

| 证据 | 说明 |
|---|---|
| `src/io/FanucLsPostProcessor.cpp:231-239` | `mnLines` + `addMnLine`；在 `UFRAME_NUM` / `UTOOL_NUM` **之前**插入 `"  ! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001)"` |
| `src/io/FanucLsPostProcessor.cpp:236-237` | 注释明确 RISK-001 draft gate；文案与计划项 4 / IMPLEMENTATION 一字不差 |
| `src/io/FanucLsPostProcessor.cpp:324-325` | `LINE_COUNT = mnLines.size()`；`MEMORY_SIZE` 同用 `mnLines.size()` —— 草稿行自然计入，无硬编码行数 |
| `src/io/FanucLsPostProcessor.cpp:337-340` | `/MN` 后 `for (i…) setw(4)<<(i+1)<<":"<<mnLines[i]<<" ;\n"` —— 草稿为 **MN 行号 1**，后续行号整体 +1，循环仍正确 |
| `src/io/FanucLsPostProcessor.cpp:241-273` | `nextPIndex` 从 1 起为 **P[] 寄存器**编号，与 MN 行号独立；运动仍为 `J P[1:…]` / `L P[2:…]`，断言 `tests/test_postprocessor.cpp:399-400` 仍锁 P 索引而非 MN 行号 |

手工推演：草稿 → UFRAME → UTOOL → Operation 注释 → 首条运动，则运动为 MN 行 5、P[1] 不变。既有单测**不**锁具体 `LINE_COUNT` 数值或 `1: UFRAME`（全库 `rg` 无此类硬锁），+1 行不破坏断言面。输出形态与 IMPLEMENTATION 声称一致：`   1:  ! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001) ;`（与既有 `! --- Operation` 注释同一 `addMnLine` 风格）。

### 2) `LsExportDialog` 常驻红条 vs `refreshPreview` — PASS

| 证据 | 说明 |
|---|---|
| `src/ui/LsExportDialog.cpp:32-42` | 独立 `QLabel* riskBanner`（parent=`this`，入 `mainLayout`）；深红底 `#5c1a1a` + `#e06c75` 边框；文案含「尚未完成示教器 / ROBOGUIDE 金样对拍」「仅为草稿」「**严禁直接作为成品下发真机**」 |
| `src/ui/LsExportDialog.cpp:44-47` | `statusLabel_` 为**另一**控件，位于横幅之下 |
| `src/ui/LsExportDialog.cpp:114-174` | `refreshPreview` **只**写 `statusLabel_` / `codeEdit_`（130、147、155、172 行），**零**引用 `riskBanner` —— 刷新校验**不会**清掉红条 |
| `src/ui/LsExportDialog.cpp:139-160` | 项 1 黄警示路径仍在：`ok && issues` → `#e5c07b`「校验通过（含警告）」；无 issue 才绿。红条与黄列/绿通过**并存、互不覆盖** |
| `src/ui/LsExportDialog.cpp:61-62` | `chkBlockCollision_->setChecked(true)` 仍为项 1 默认（本轮未削弱） |

横幅为局部栈指针、非成员：Qt 父子所有权保证生命周期；常驻静态文案无需后续改写。预览 `codeEdit_` 来自 `generateLsString`（137 行），故 UI 预览文本亦含水印。

### 3) 三断言 — PASS

| 证据 | 说明 |
|---|---|
| `tests/test_postprocessor.cpp:375-376` | GATE-004：中文路径 `exportToFile` 读回内容含 `! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001)` |
| `tests/test_postprocessor.cpp:391-392` | GATE-004：`generateLsString` 导出文本含水印 |
| `tests/test_postprocessor.cpp:393-397` | GATE-004：`draftPos > mnPos`（水印出现在 `/MN` 之后） |
| `rg -c FIX-EXPORT-GATE-004` | 恰好 **3** 处，与 IMPLEMENTATION / STATUS 声称一致 |

第三条不锁「必须为 MN 行号 1」，但源码插入点已是首条 `addMnLine`（见上）。中文路径断言嵌在 `if (chineseOk)` 内：若中文写出失败则 GATE-003 已先失败；Windows 783/0 且显式列出该断言 PASS，说明该路径已实测走到。

### 4) OpwKinematics 冻结 — PASS

| 证据 | 说明 |
|---|---|
| `git status` / `git diff --name-only` | **无** `OpwKinematics.*` |
| `git hash-object` == `git rev-parse HEAD:…` | `src/algo/OpwKinematics.cpp` = `73b34cc988d5e5a838b50d806f409e8693922670`；`include/olp/algo/OpwKinematics.h` = `e02edf15938b1130bb62092539e2802282ac9ccc` —— 与提交树**完全一致** |
| mtime | Opw `2026-10-29 13:56`（基线）；Fanuc/LsDialog/test_postprocessor `20:55`（本项编辑窗） |

未改耦合、`offsetsRad`、`signCorrections`、turns。

---

## 强制连带排查清单 A–F

### A. FANUC `/MN` 行号 / `LINE_COUNT` — **PASS（无缺陷）**

插入草稿后：`LINE_COUNT`/`MEMORY_SIZE` 与编号循环均基于同一 `mnLines` 容器（`:324-325`、`:337-340`）。P[] 索引用独立 `nextPIndex`（`:241-248`），运动指令断言仍以 `P[1:APPROACH]` 等为准（`:399`），不被 MN 行号平移破坏。未见「插注释却硬编码旧行号」类错误。

### B. `exportControllerPrograms` 多程序 — **PASS（同带水印）**

| 证据 | 说明 |
|---|---|
| `FanucLsPostProcessor.cpp:415-447` | 逐程序拼 UTF-8 路径后调 `exportToFile` |
| `FanucLsPostProcessor.cpp:383-389` | `exportToFile` → `generateLsString` |
| `FanucLsPostProcessor.cpp:236-237` | 水印在 `generateLsString` 内无条件分支（校验通过才生成正文） |

故多程序路径**只要写出成功即含水印**。`tests/test_postprocessor.cpp:455-460` 只断言两文件存在、**未再读回水印**——覆盖缺口属 **P2 测弱**（产品路径已正确），**不构成 P0/P1**，不据此 CHANGES_REQUESTED。`MainWindow::exportAllPrograms`（`:551-566`）同样经 `exportControllerPrograms`。

### C. SampleSession → `WELD_001.ls` — **PASS（经 `exportToFile` 即含水印）**

| 证据 | 说明 |
|---|---|
| `src/ui/SampleSession.cpp:221-239` | 门禁拒绝则**不写**；否则 `FanucLsPostProcessor::exportToFile(sampleProg, lsOut)` |
| 调用链 | `exportToFile` → `generateLsString` → 水印行 |

成功写出时必含水印。Windows 证据（采信 IMPLEMENTATION §四）：`build/WELD_001.ls` 含 `1:  ! DRAFT - UNVERIFIED ON ROBOGUIDE (RISK-001) ;`。box 无该文件、未复跑样例。若门禁拒绝写出，则无「无水印正式文件」旁路——与项 1 拒写语义一致。

### D. UI 红条与 Warning 黄列并存 / `refreshPreview` 是否清横幅 — **PASS**

红条（`:32-42`）与 `statusLabel_`（`:45-47`）分离；`refreshPreview` 仅改后者（见上）。黄警示（项 1，`:146-153`）与红条可同时可见；绿「语法与运动学校验通过」（`:155-160`）亦不清除红条。设计分层：语法/门禁校验通过 ≠ RISK-001 已关闭。非互清缺陷。

### E. 是否误改 Opw 或削弱项 1–3 — **PASS（未削弱）**

| 检查 | 结果 |
|---|---|
| Opw | 哈希一致，见上 |
| 项 1 默认 | `FanucLsPostProcessor.h:15-16` 仍 `blockOnCollision{true}` / `blockOnConfigJump{true}`；`LsExportDialog.cpp:62` 勾选 true；GATE-001 断言 `:214-229` 仍在 |
| 项 2 扫掠写回 | `CollisionAndSim.cpp` 仍含 Near 不短路 / `upgradeTargetCacheFromSegmentIssue`；GATE-002 断言在 `tests/test_collision.cpp:182-280` |
| 项 3 utf8ToPath | `exportToFile` `:395-402` 仍用 `utf8ToPath`；GATE-003 锁测 `:249-374` 仍在；本项仅在中文路径块**追加**水印断言 |

本项 diff 对 Fanuc 核心仅 **+2 行水印**（另含项 1–3 已 APPROVED 存量）。未发现放宽默认或删除锁测。

### F. P0/P1 连带缺陷 → 必须 CHANGES_REQUESTED — **未发现**

主动搜索后的残留均为已知/可接受层级：
- RISK-001 **仍 OPEN**（水印/UI 不修正运动学符号——计划明示）。
- 多程序/MainWindow 导出测未重复断言水印字符串（**P2 覆盖**）。
- 绿「校验通过」与红「严禁下发」并存（分层提示，非清横幅）。
- 累计项 1–4 工作树 diff 待 **FINAL** 全量审。
- box 未复跑；采信 Windows 783/0。

**无 P0/P1 → 不改为 CHANGES_REQUESTED。**

---

## 与文档一致性

- **PLAN 项 4**：三条交付 + Opw 冻结全部落地；非目标（不改 HTML、不拆 CommandManager、不改根 Plan/DIR-001、`PlanDocs/STATUS.md` 仍 IDLE）遵守。
- **IMPLEMENTATION.md**：声明与真实 diff 对齐；水印位置、横幅不被 `refreshPreview` 覆盖、三断言标签、Windows 783/0 / GATE-004 文案列表可核对；未声称关闭 RISK-001；未声称 box 已编译。
- **设计基线**：草稿门禁服务于「未对拍不得当真机成品」的安全可见性；与 v1.1 强调 ROBOGUIDE/真机验收前提一致，且**不**假装已完成对拍。
- **工作树**仍含项 1–3 已审 diff；本轮只审项 4 增量与连带，不重开已 APPROVED 项之结论，但已用断言与哈希做回归抽查。

---

## 残留风险（不挡本项 APPROVED）

1. **RISK-001 仍 OPEN**：水印与 UI 仅草稿门禁，不消除 J2/J3 耦合/符号偏差。
2. **FINAL 未开**：项 1–4 累计 diff 须另开 `FIX-EXPORT-GATE-FINAL` 全量独立审查后，本安全门禁计划才可关闭。
3. **测覆盖 P2**：`exportControllerPrograms` / `MainWindow::exportFanucLs` 未单独 `find` 水印（逻辑上必经 `generateLsString`）。
4. **box 无编译证据**；采信用户机 Windows 结果。仓库内无 `WELD_001.ls` 可二次打开核对。
5. UI：无警告时绿通过文案紧邻红条——可能被粗心用户误读为「可下发」；红条文案已足够醒目，属体验残留而非本项漏改。

---

## 给协调方（可立刻 priority）

| 字段 | 值 |
|---|---|
| Verdict | **APPROVED** |
| 可否开 FINAL | **可以**开 `FIX-EXPORT-GATE-FINAL`（本端未开） |
| 范围 1–4 | **全部 PASS** |
| 连带 A–F | **全部 PASS / 无 P0/P1** |
| 修改点（若 CHANGES） | 无 |
| 关键证据（≥5） | `FanucLsPostProcessor.cpp:236-237`；`:324-325`；`:337-340`；`LsExportDialog.cpp:32-42`；`:114-174`（仅改 statusLabel_）；`test_postprocessor.cpp:375-376,391-397`；Opw 哈希=HEAD |
| 残留 | RISK-001 OPEN；待 FINAL；box 未复跑 |

---

# 审核结论 — FIX-EXPORT-GATE-003 Round 1

> **Task ID**：`FIX-EXPORT-GATE-003`
> **Task**：门禁单测加固与 `.ls` UTF-8 路径（P1-7 部分 + 项1/2 锁测）
> **Round**：1
> **Reviewer**：Si_FastSuite_Audit_X（独立审核执行端）
> **Reviewed at**：2026-10-29 Asia/Singapore (UTC+8)
> **Executor**：执行方（Si_FastSuite）
> **Verdict**：**APPROVED**
>
> 本轮为独立深度审核：通读 `include/olp/io/Utf8Path.h`、`src/io/Utf8Path.cpp` 全文；细读 `git diff` 中 `ProjectSerializer.cpp`（删本地匿名 `utf8ToPath`）、`FanucLsPostProcessor.cpp` 的 `exportToFile` / `exportControllerPrograms`、`CMakeLists.txt` 入链、`tests/test_postprocessor.cpp` 全部 FIX-EXPORT-GATE-003 断言块；核对 `tests/test_tjoint_regression.cpp` **无 diff**（可选未做）。对照权威：`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md` **仅项 3**、`CRDocs/IMPLEMENTATION.md`、`STATUS.md`（审前 READY_FOR_REVIEW）、设计基线 `Docs/OLP_System_Design_Claude.md` v1.1 相关。**未改**工程业务/测试源码；**禁止 CloudAgent**；**未开项 4**；仅更新本文件与 `STATUS.md`。Windows 实测采信用户机（DESKTOP-L4AMHKJ，~20:30 Asia/Singapore 2026-10-29，`Compile.bat --reconfigure` exit 0，**780 passed / 0 failed**）；**采信用户机，本审核未在 box 复跑**。

## 总判定

| 项 | 结论 |
|---|---|
| 1. `tests/test_postprocessor.cpp` 锁定默认拒止：端点 Collision、段中扫掠 Collision（含 sim 写回后）、ConfigJump | **PASS** |
| 2. `FanucLsPostProcessor::exportToFile`（及 `exportControllerPrograms`）与 `ProjectSerializer` **共享** `utf8ToPath` | **PASS** |
| 3. 中文路径 `.ls` 导出单测 | **PASS** |
| 4. 可选 tjoint 收紧 | **非目标（执行方明确未做）** — 不判漏改、不据此 CHANGES_REQUESTED |

三条强制范围全部实质达标，与计划项 3 及 `IMPLEMENTATION.md` 声明一致，未见夸大。可选 tjoint 按计划允许跳过且已记入 IMPLEMENTATION §一.4。**Verdict = APPROVED**。项 4 明确未开，见残留风险。

---

## 逐条证据（≥3 条 `文件:行号` / 条）

### 1) 默认门禁锁测（端点 Collision / 段中扫掠 Collision / ConfigJump）— PASS

| 证据 | 说明 |
|---|---|
| `tests/test_postprocessor.cpp:252-262` | 默认 `LsPostConfig{}` 仍启用 `blockOnCollision`/`blockOnConfigJump`；端点 `valid=true`+`status=Collision` → `validateProgram` !ok + `generateLsString` 空 |
| `tests/test_postprocessor.cpp:264-271` | 端点 `ConfigJump`（`valid=true`）→ 默认拒写 |
| `tests/test_postprocessor.cpp:273-284` | 写回形状：首点 Valid、次点 `valid=true`/`status=Collision` → 默认拒写（锁项 2 语义） |
| `tests/test_postprocessor.cpp:337-354` | 活扫掠：`validateAndSimulateProgram` 写回 `Collision` 且 `valid` 仍 true → 默认 `validateProgram`/`generateLsString` 拒导出 |
| `include/olp/io/FanucLsPostProcessor.h:15-16` | 产品默认仍为 `blockOnCollision{true}` / `blockOnConfigJump{true}`（项 1 已 APPROVED；本项锁测不改默认） |

断言文案均含 `FIX-EXPORT-GATE-003:`，与 IMPLEMENTATION §四 所列 Windows 显式 PASS 一致。活扫掠几何与 GATE-002 Test 9 同源（home→endJoints[+0.4]、门盒 `half=6`、偏置 95mm），见残留风险。

### 2) 共享 `utf8ToPath`（导出与工程序列化同语义）— PASS

| 证据 | 说明 |
|---|---|
| `include/olp/io/Utf8Path.h:11` / `src/io/Utf8Path.cpp:19-30` | 公共 `olp::io::utf8ToPath`：Windows `MultiByteToWideChar(CP_UTF8)`→`path(wstring)`；非 Windows `u8path` — 与原 `ProjectSerializer` 匿名实现同语义 |
| `CMakeLists.txt:123` | `olp_io` 增加 `src/io/Utf8Path.cpp` |
| `src/io/ProjectSerializer.cpp:6` + `:803` / `:872` | `#include "olp/io/Utf8Path.h"`；`saveToFile`/`loadFromFile` 调用共享函数；本地匿名 `utf8ToPath` 已从 diff 删除（仓库内仅剩 `Utf8Path.*` 定义） |
| `src/io/FanucLsPostProcessor.cpp:2` + `:393-400` | `#include Utf8Path.h`；`exportToFile`：`utf8ToPath` + `ofstream(path)`（不再 `path(string)`/`ofstream(string)`） |
| `src/io/FanucLsPostProcessor.cpp:424-436` | `exportControllerPrograms`：目录 `utf8ToPath(outputDir)`；子路径 **UTF-8 字符串拼接**（避免 `path::string()` CP936 损毁），再交 `exportToFile` |

全库 `std::filesystem::path utf8ToPath` 定义仅存于 `Utf8Path.h/cpp`，满足「共享、非双份」。

### 3) 中文路径 `.ls` 导出单测 — PASS

| 证据 | 说明 |
|---|---|
| `tests/test_postprocessor.cpp:358-374` | 路径含 `olp_ls_中文路径导出_001/工位导出_WELD_001.ls`；`exportToFile` 成功 + `filesystem::exists(utf8ToPath(...))` |
| `tests/test_postprocessor.cpp:370-374` | 读回内容含 `/PROG  TEST_PROG` |
| `tests/test_postprocessor.cpp:376-377` | 清理用 `utf8ToPath(chineseDir)`，与产品路径语义一致 |

Windows 实测（采信）显式 PASS：Chinese-path `exportToFile` 与 `/PROG` 内容。依赖源文件 UTF-8 + MSVC `/utf-8`（与 M14-010 同类），见残留风险。

### 4) 可选 tjoint 收紧 — 非目标（不判 FAIL）

| 证据 | 说明 |
|---|---|
| `git diff -- tests/test_tjoint_regression.cpp` | **0 行**（本项未改） |
| `tests/test_tjoint_regression.cpp:252-262` | `allTargetsValid` 仍仅排除 Unreachable/JointLimitExceeded/ConfigJump，**未**要求 `status == Valid` |
| `CRDocs/IMPLEMENTATION.md` §一.4 / §二 | 执行方明确 prefer leave out；产品门禁未放宽 |

按任务说明与计划「可选」条款：**不得**因未做判 CHANGES_REQUESTED。

---

## 与文档一致性

- **PLAN 项 3**：三条强制范围均已落地；可选 tjoint 跳过且文档化；非目标（项 4、不改 OpwKinematics、不改 HTML、不拆 CommandManager、不改根 Plan / DIR-001、`PlanDocs/STATUS.md` 保持 IDLE）遵守——核对 `PlanDocs/STATUS.md` 仍为 IDLE。
- **IMPLEMENTATION.md**：声明与真实 diff 对齐；明确 box 未编译、Windows 780/0 与 GATE-003 断言文案列表；未把「可选已做」或「项 4 已做」写成完成。§一.2「未改 FanucLsPostProcessor.h 公开 API / 默认」指本轮不重开项 1 默认——working tree 中该头文件相对 baseline 的改动属项 1 已 APPROVED 存量，本项锁测消费之，未见夸大。
- **设计基线**：UTF-8 路径与后处理门禁锁测服务于真机导出安全可见性；本项不声称关闭 RISK-001。
- **项 1/2 依赖**：默认拒 Collision/ConfigJump（项 1）+ 扫掠写回 cache（项 2）已 APPROVED；本项在 `test_postprocessor` 加锁并修 `.ls` 路径。未草率复用空话——本轮通读了 Utf8Path 全文、导出两函数与 GATE-003 断言块。
- **工作树仍含项 1/2 已审 diff**（`CollisionAndSim` / `LsExportDialog` / `SampleSession` / `test_collision` 等）；本轮只审项 3 相关路径，不把项 1/2 结论重开。

---

## 构建与测试证据（采信用户机；box 未复跑）

- 机器：`DESKTOP-L4AMHKJ`；时间：2026-10-29 ~20:30 Asia/Singapore (UTC+8)
- `Tools\Compile.bat --reconfigure` **exit 0**
- `SiFastSuiteOLP_AllTests`：**780 passed / 0 failed**
- 显式 PASS（与源码断言文案一致）：默认门禁锁、端点 Collision、ConfigJump、写回形状拒导出、活扫掠写回 Collision/`valid` true、写回后拒导出、Chinese-path `exportToFile`、`/PROG` 内容
- **采信用户机，本审核未在 box 复跑**（box 无完整 Qt+OCCT+vcpkg Windows 工具链）。静态 Read / `git diff` / `rg` 已核对实现与断言。

---

## 残留风险（APPROVED 必须列）

1. **项 4 未开**：`.ls` RISK-001 草稿水印与对话框常驻提示仍待做；**勿在本 STATUS=APPROVED 前由审核方开项**。
2. **RISK-001 仍 OPEN**：运动学符号/耦合冻结；本项不关闭。
3. **可选 tjoint 收紧未做**：`allTargetsValid` 仍不要求 `status==Valid`；Collision/Near 目标在 tjoint 回归中仍可能绿。非本项缺口。
4. **活扫掠锁测与 GATE-002 Test 9 同源**：若某机 FCL/步数未撞到门，GATE-003 活扫掠与 Test 9 会一并失败（非静默放宽）；写回形状断言不依赖 FCL，仍锁后处理。
5. **中文路径单测**：`temp_directory_path().string()` 在极端非 ASCII 用户目录下可能与 UTF-8 字面量拼接混码；常见 Temp 为 ASCII。依赖 `/utf-8` 编译。`exportControllerPrograms` 用 `/` 拼接 UTF-8，Windows 接受且最终仍走 `utf8ToPath`。
6. **`ProjectSerializer.cpp` 残留 `#include <windows.h>`**：抽离 `utf8ToPath` 后 Win32 头可能不再需要；属卫生项，不影响行为，不挡 APPROVED。
7. **工作树累计项 1+2+3 diff 未提交**：全量 `FIX-EXPORT-GATE-FINAL` 审查仍待四项均 APPROVED 后另开。

---

## 本轮范围内无 CHANGES_REQUESTED

三条强制 PASS + 可选明确非目标 → **APPROVED**。下一步：仅当本 STATUS=APPROVED 后由执行方开项 4（`FIX-EXPORT-GATE-004`）。

---

# 审核结论 — FIX-EXPORT-GATE-002 Round 1

> **Task ID**：`FIX-EXPORT-GATE-002`
> **Task**：段内扫掠短路拆分 + 段内问题回写 `target.cache`（P0-2 + D.1）
> **Round**：1
> **Reviewer**：Si_FastSuite_Audit_X（独立审核执行端）
> **Reviewed at**：2026-10-29 Asia/Singapore (UTC+8)
> **Executor**：执行方（Si_FastSuite）
> **Verdict**：**APPROVED**
>
> 本轮为独立深度审核：通读 `src/algo/CollisionAndSim.cpp` 中 `validateAndSimulateProgram` 全文（含扫掠循环与端点校验）、匿名 `cacheSeverityTier` / `upgradeTargetCacheFromSegmentIssue`，以及 `tests/test_collision.cpp` Test 9 / 9b 全文；细读 `git diff` 对本项相关 hunk（CollisionAndSim + collision 单测 + 注册）。追踪链路：扫掠采样 → Near/Hard 标志 → `sim.issues` → `target.cache` 回写 → `FanucLsPostProcessor::validateProgram` 读 `cache.status` 拒写。对照权威：`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md` **仅项 2**、`CRDocs/IMPLEMENTATION.md`、设计基线「离散 + 段内采样防端点不撞中间撞」。**未改**工程业务/测试源码；**禁止 CloudAgent**；**未开项 3/4**；仅更新本文件与 `STATUS.md`。Windows 实测采信用户机（DESKTOP-L4AMHKJ，~17:14 Asia/Singapore 2026-10-29）；**采信用户机，本审核未在 box 复跑**。

## 总判定

| 项 | 结论 |
|---|---|
| 1. `postedSweepIssue` 拆分：Near 不得短路同段后续硬撞；`sweepCollided` 记/升级 Collision | **PASS** |
| 2. 段内 Collision / L 段 Unreachable / 段内 ConfigJump(>15°) 按优先级回写 `target.cache.status` + `issueMessage` | **PASS** |
| 3. 不改 `cache.valid`=IK 存在语义（碰撞点可仍 valid=true） | **PASS** |
| 4. 单测：Test 9 段中硬撞→默认 `validateProgram` !ok；Test 9b Near 不短路硬撞且最终 Collision | **PASS** |

四条范围全部实质达标，与计划项 2 及 `IMPLEMENTATION.md` 声明一致，未见夸大。**Verdict = APPROVED**。项 3/4 明确未做，见残留风险。

---

## 逐条证据（≥3 条 `文件:行号` / 条）

### 1) `postedSweepIssue` 拆分 + Near 不短路硬撞 — PASS

| 证据 | 说明 |
|---|---|
| `src/algo/CollisionAndSim.cpp:274-276` | 旧单一 `postedSweepIssue` 拆为 `postedHardSweepCollision` + `postedNearSweepIssue` |
| `src/algo/CollisionAndSim.cpp:348-349` | 扫掠门控仅 `!postedHardSweepCollision`；Near 后仍继续扫 |
| `src/algo/CollisionAndSim.cpp:356-365` | `sweepCollided` → 推 `Collision` issue + Hard 回写 cache；置 `postedHardSweepCollision` |
| `src/algo/CollisionAndSim.cpp:366-378` | 仅 clearance 不足 → Near（一次）；`postedNearSweepIssue` **不**阻止后续硬撞分支 |

旧逻辑「Near 或 Hard 任一命中即 `postedSweepIssue=true`」已消除。与 PLAN「NearCollision 不得短路同段后续硬碰撞；`sweepCollided` 必须升级记录 Collision」一致。

### 2) 段内问题按优先级回写 cache — PASS

| 证据 | 说明 |
|---|---|
| `src/algo/CollisionAndSim.cpp:25-38` | `cacheSeverityTier`：Hard=`Unreachable|Collision|ConfigJump|JointLimitExceeded`；Near=`NearCollision`；其余 Soft |
| `src/algo/CollisionAndSim.cpp:40-49` | `upgradeTargetCacheFromSegmentIssue`：仅新 tier **严格更高** 时写 `status`+`issueMessage`；**不改** `valid` |
| `src/algo/CollisionAndSim.cpp:309-310` | L 段无逆解 → `Unreachable` Hard 回写 |
| `src/algo/CollisionAndSim.cpp:330-331` | L 段关节步 >15° → `ConfigJump` Hard 回写 |
| `src/algo/CollisionAndSim.cpp:364-365` / `:376-377` | 扫掠 Hard/Near 回写；Hard 可升级 Near |

优先级 Hard > Near > Soft（含 Valid）与计划一致。`JointLimitExceeded` 归 Hard 属合理加固（防扫掠 Near 降级端点硬状态），计划字面未单列，记入残留说明而非缺口。

调用链闭环：`for (auto& target : op.targets)`（`:178`）→ 段内 upgrade 写同一 `target` 引用 → `inOutProgram` 上 cache 对后处理可见。

### 3) `cache.valid` 语义未改 — PASS

| 证据 | 说明 |
|---|---|
| `src/algo/CollisionAndSim.cpp:48` | 注释明确：leave `cache.valid` unchanged（= IK 存在） |
| `src/algo/CollisionAndSim.cpp:231` | 有 IK 时端点仍 `target.cache.valid = true`，再可能被 status 标 Collision |
| `tests/test_collision.cpp:190-191` | Test 9：段中 Collision 回写后仍要求 `endCache.valid` |
| `tests/test_collision.cpp:279-280` | Test 9b：Near→Collision 升级后 `valid` 仍 true |

碰撞由 `status` 表达、后处理按 status 阻断（项 1 已 APPROVED 的 `blockOnCollision`）；本项未把碰撞点改成 `valid=false`。与 PLAN/IMPLEMENTATION 一致。

### 4) 单测锁定（Test 9 / 9b）— PASS

| 证据 | 说明 |
|---|---|
| `tests/test_collision.cpp:184-194` | Test 9：`sweepHit` + `cache.status==Collision` + `valid` + 默认 `validateProgram` → `!ok && hasErrors()` |
| `tests/test_collision.cpp:271-280` | Test 9b：`sawHard` + 最终 `cache.status==Collision` + `valid`；detail 记录是否见过 Near |
| `tests/test_support.h:21` / `tests/test_main.cpp:47` | `testNearSweepDoesNotShortCircuitHardCollision` 已声明并注册 |
| `src/io/FanucLsPostProcessor.cpp:109-116` | 默认 `blockOnCollision` 下 `cache.status==Collision` → Error（项 1 依赖；Test 9 闭合链路） |

Windows 实测（采信）：`Compile.bat` exit 0；**771 passed / 0 failed**；显式 PASS 含 mid-segment Collision 回写、`valid` 仍 true、默认 validateProgram 拒段中撞、9b Near 不短路且最终 Collision；`--verify` 仍写出仅 Near 的 `WELD_001.ls`（无硬撞，符合设计）。

---

## 与文档一致性

- **PLAN 项 2**：四条范围均已落地；非目标（项 3/4、不改 OpwKinematics、不改 HTML 架构、不拆 CommandManager）遵守——本项 diff 主战场为 `CollisionAndSim.cpp` + collision 单测/注册。
- **IMPLEMENTATION.md**：声明与真实 diff 对齐；明确 L 段 Unreachable/ConfigJump 有代码路径、本轮无独立几何单测——无夸大「已有专用几何单测」。
- **设计基线**：段内采样防「端点不撞中间撞」（OLP 设计 M10/校验相关）与本项回写+门禁可见数据面一致；本项不声称关闭 RISK-001。
- **项 1 依赖**：默认 `validateProgram` 拒 Collision 已 APPROVED；本项负责把段中扫掠写入 `target.cache.status`，使该门禁能看见段中硬撞。未草率复用项 1 空话——本轮通读了 CollisionAndSim 全文与本项 hunk。
- **PlanDocs/STATUS.md** 仍应保持 IDLE（执行方声明未改）；根 Plan / DIR-001 未在本项触及。

---

## 构建与测试证据（采信用户机；box 未复跑）

- 机器：`DESKTOP-L4AMHKJ`；时间：2026-10-29 ~17:14 Asia/Singapore (UTC+8)
- `Tools\Compile.bat` **exit 0**
- `SiFastSuiteOLP_AllTests`：**771 passed / 0 failed**
- 显式 PASS（与源码断言文案一致）：mid-segment hard sweep → Collision 回写；`cache.valid` 仍 true；默认 `validateProgram` 拒段中撞；9b Near 不短路硬撞且最终 Collision
- `--verify` 仍写出 `WELD_001.ls`：仅 Near（clearance below safe），无硬撞阻断——符合 Near=Warning 可导出设计
- **采信用户机，本审核未在 box 复跑**（box 无完整 Qt+OCCT+vcpkg Windows 工具链）。静态 Read/`git diff`/`rg` 已核对实现与断言。

---

## 残留风险（APPROVED 必须列）

1. **L 段 Unreachable/ConfigJump 回写无独立几何单测**：代码已接 `upgradeTargetCacheFromSegmentIssue`（`:309-310` / `:330-331`），但本轮验收单测聚焦扫掠 Collision（Test 9/9b）。项 3 可加锁测。
2. **Test 9b 几何依赖放大 `collisionSafeClearanceMm=120`**：以制造「先 Near 后 Hard」采样序；断言以 `sawHard`+status=Collision 为主，`sawNear` 仅 detail。结构上 Near 标志已不短路，但若某机采样从未触发 Near issue，测试仍可通过——不削弱 Hard 路径覆盖。
3. **同 Hard 档位不互覆盖**：`upgrade` 用严格更高 tier；若端点已是 `ConfigJump`/`Collision`，后续同档扫掠 Hard 不改 `issueMessage`。默认门禁下两者均可拒导出，安全不漏；消息文案可能保留先到者。
4. **Soft 状态可被扫掠 Near 升级**：`Singularity`/`NearJointLimit`/`Valid` 属 Soft；段中 Near 可覆盖端点 Singularity。计划未要求保护 Singularity，属可接受行为，UI/诊断时需知。
5. **项 3/4 未开**：UTF-8 路径加固、RISK-001 `.ls` 水印仍 OPEN；RISK-001 运动学符号仍冻结。不挡本项 APPROVED。
6. **工作树仍含项 1 已审 diff**：`FanucLsPostProcessor*` / `LsExportDialog` / `SampleSession` / `test_postprocessor` 等与本项并存于 working tree；本轮只审项 2 相关路径，不把项 1 结论重开。

---

## 本轮范围内无 CHANGES_REQUESTED

四条 PASS → **APPROVED**。下一步：仅当本 STATUS=APPROVED 后由执行方开项 3（`FIX-EXPORT-GATE-003`）。

---

# 审核结论 — FIX-EXPORT-GATE-001 Round 1

> **Task ID**：`FIX-EXPORT-GATE-001`
> **Task**：后处理默认门禁（P0-1 + P0-3）：`blockOnCollision`/`blockOnConfigJump` 默认拒止、NearCollision/Singularity Warning、LsExportDialog 黄警示、SampleSession 拒写正式 WELD_001.ls、单测锁定默认配置
> **Round**：1
> **Reviewer**：Si_FastSuite_Audit_X（独立审核执行端）
> **Reviewed at**：2026-10-29 Asia/Singapore (UTC+8)
> **Executor**：执行方（Si_FastSuite）
> **Verdict**：**APPROVED**
>
> 本轮为独立深度审核：通读 working tree `git diff` 与相关函数全文（非仅 hunk），追踪 `validateProgram` → `generateLsString` → `exportToFile`、`LsExportDialog` OK/Warning 路径、`SampleSession` 写 `WELD_001.ls` 路径及单测断言。对照权威：`PlanDocs/FIX-EXPORT-SAFETY-PLAN.md` 项 1、`CRDocs/IMPLEMENTATION.md`、设计基线 M12 后处理相关。**未读取**任何 `Si_FastSuite_Audit_Report*.md`。**未改**业务/测试源码；**未开项 2**；仅更新本文件与 `STATUS.md`。测试结果采信用户机（DESKTOP-L4AMHKJ）实测写入 `IMPLEMENTATION.md` §四，本审核未在 box 复跑 Windows 编译/ctest。

## 总判定

| 项 | 结论 |
|---|---|
| 1. `LsPostConfig` 默认 `blockOnCollision`/`blockOnConfigJump` = true | **PASS** |
| 2. `validateProgram`：ConfigJump 门禁开→Error；NearCollision/Singularity→Warning | **PASS** |
| 3. `LsExportDialog`：碰撞阻断默认勾选；ok+Warning 黄列；ConfigJump 保持默认门禁 | **PASS** |
| 4. `SampleSession`：默认校验失败/阻断优先失败返回，不静默写正式 `WELD_001.ls` | **PASS** |
| 5. `tests/test_postprocessor.cpp` 锁定默认拒止 Collision + ConfigJump | **PASS** |

五项范围全部实质达标，与 `FIX-EXPORT-SAFETY-PLAN.md` 项 1 及 `IMPLEMENTATION.md` 声明一致，未见夸大。**Verdict = APPROVED**。项 2（扫掠→`target.cache`）明确未做，见下方残留风险（不阻断本项通过）。

---

## 逐条证据（≥3 条 `文件:行号` / 条）

### 1) `LsPostConfig` 默认 true + `blockOnConfigJump` — PASS

| 证据 | 说明 |
|---|---|
| `include/olp/io/FanucLsPostProcessor.h:15` | `bool blockOnCollision{true};`（旧默认 false，diff 已改） |
| `include/olp/io/FanucLsPostProcessor.h:16` | 新增 `bool blockOnConfigJump{true};`，位于 Collision 与 Unreachable 之间 |
| `tests/test_postprocessor.cpp:207-208` | `check(LsPostConfig{}.blockOnCollision && LsPostConfig{}.blockOnConfigJump, ...)` 锁定默认 |

与 PLAN「默认改为 true / 新增 blockOnConfigJump{true}」及 IMPLEMENTATION §1 一致。

### 2) `validateProgram` 严重度分流 — PASS

| 证据 | 说明 |
|---|---|
| `src/io/FanucLsPostProcessor.cpp:109-116` | `Collision` 仅在 `config.blockOnCollision` 时记 **Error**（默认已开） |
| `src/io/FanucLsPostProcessor.cpp:118-127` | `ConfigJump`：`blockOnConfigJump` → Error，否则 Warning |
| `src/io/FanucLsPostProcessor.cpp:129-145` | `NearCollision` / `Singularity` → **Warning**（不挡 `ok`） |
| `src/io/FanucLsPostProcessor.cpp:202-203` | `result.ok = !result.hasErrors();` Warning 不置 `ok=false` |
| `src/io/FanucLsPostProcessor.cpp:210-220` | `generateLsString` 先 `validateProgram`；`!ok` 返回空串拒写 |
| `src/io/FanucLsPostProcessor.cpp:380-389` | `exportToFile` → `generateLsString`，空 body 返回 false |

调用链闭环：默认配置下端点 Collision/ConfigJump → Error → `ok=false` → 拒生成/拒导出；NearCollision/Singularity → Warning → 可导出。与 PLAN/IMPLEMENTATION 一致，无夸大。

### 3) `LsExportDialog` UI 门禁与黄警示 — PASS

| 证据 | 说明 |
|---|---|
| `src/ui/LsExportDialog.cpp:49-50` | `chkBlockCollision_->setChecked(true)`（旧 false） |
| `include/olp/ui/LsExportDialog.h:29` | `io::LsPostConfig config_{};` 成员默认含 `blockOnConfigJump=true` |
| `src/ui/LsExportDialog.cpp:103-106` | `refreshPreview` 只写回 collision/arc/home 四字段，**不覆盖** `blockOnConfigJump` |
| `src/ui/LsExportDialog.cpp:127-149` | `ok && !issues.empty()` → 黄色「校验通过（含警告，可导出）」+ 明细 `#e5c07b`；仅无 issue 时纯绿 |
| `src/ui/LsExportDialog.cpp:123-125,199` | 预览与 `exportToFile` 均使用同一 `config_` |

无 ConfigJump 勾选框时依赖默认门禁——符合计划「keep config default」。禁止纯绿掩盖 Warning 已落实。

### 4) `SampleSession` 优先失败返回 — PASS

| 证据 | 说明 |
|---|---|
| `src/ui/SampleSession.cpp:206-215` | 仿真 `blockingIssue`：Collision / Unreachable / JointLimitExceeded / ConfigJump / `hasConfigJumpInLinear` |
| `src/ui/SampleSession.cpp:221-223` | 默认 `validateProgram(sampleProg)`；`refuseProductionLs = !lsVal.ok \|\| blockingIssue` |
| `src/ui/SampleSession.cpp:227-239` | 拒写分支：不调用 `exportToFile`；仅非拒绝时写 `build/WELD_001.ls` |
| `src/ui/SampleSession.cpp:243-250` | 拒绝时 `exitCode=2`，summary「导出门禁拒绝写出正式 WELD_001.ls」；旧「文件仍已写出」文案已删除 |

选择「优先失败返回」符合 PLAN。HTML 预览路径保留。注：当仅有段内扫掠问题、端点 cache 仍 Valid 时，靠 `blockingIssue`（sim.issues）拒写——属对本项门禁的额外弥补，不能替代项 2 的 cache 回写。

### 5) 单测锁定默认拒止 — PASS

| 证据 | 说明 |
|---|---|
| `tests/test_postprocessor.cpp:207-208` | 默认两开关均为 true |
| `tests/test_postprocessor.cpp:209-213` | 默认配置 + Collision → `!ok` + `generateLsString` 空 |
| `tests/test_postprocessor.cpp:215-221` | 默认配置 + ConfigJump → Error 拒导出 |
| `tests/test_postprocessor.cpp:223-227` | `blockOnConfigJump=false` → Warning 可导出 |
| `tests/test_postprocessor.cpp:229-239` | NearCollision / Singularity → Warning 可导出 |

静态验证测试文案与 IMPLEMENTATION §四所列 PASS 名称一致。

---

## 与文档一致性

- **PLAN 项 1**：五条范围均已落地；非目标（不改 CollisionAndSim 扫掠）遵守——`src/algo/*` 无本轮 diff。
- **IMPLEMENTATION.md**：声明与真实 diff 对齐，无夸大「项 2 已做」；明确未改 OpwKinematics / HTML 架构 / 根 Plan。
- **设计基线 M12**：命名校验、输出预览、后处理配置等与本次默认门禁加固方向一致；本项不声称关闭 RISK-001。
- **PlanDocs/STATUS.md** 仍为 IDLE；根 `Plan.md` / `.claude/Plan.md` 未改——符合独立新计划约束。

---

## 构建与测试证据（采信用户机；box 未复跑）

- 机器：`DESKTOP-L4AMHKJ`，路径 `C:\Users\mimovr\Desktop\TEmp\Si_FastSuite`
- 时间：2026-10-29 约 16:17–16:19 Asia/Singapore (UTC+8)
- 重建：`FanucLsPostProcessor.obj` / `test_postprocessor.obj` / `olp_tests.exe` / `SiFastSuiteOLP.exe`
- `LastTest.log`：`SiFastSuiteOLP_AllTests` **765 passed, 0 failed**
- 显式 PASS（与源码断言文案一致）：默认启用两门禁；默认拒 Collision；默认拒 ConfigJump；`blockOnConfigJump=false` 时 ConfigJump 为 Warning 可导出；NearCollision/Singularity 默认 Warning 可导出
- **测试结果采信用户机实测，本审核未在 box 复跑**（box 无完整 Qt+OCCT+vcpkg Windows 工具链）。静态 `rg`/Read 已确认测试代码锁定默认行为。

---

## 残留风险（必须；项 2 未做）

1. **项 2 未做（P0 残留）**：`CollisionAndSim.cpp` 段内扫掠（约 `postedSweepIssue` / `sweepCollided`，见 `src/algo/CollisionAndSim.cpp:234-315`）仅向 `sim.issues` 推送 Collision/NearCollision，**不回写**受影响 `target.cache.status`。后处理 `validateProgram` 只看端点 cache。因此「首尾 Valid、段中硬撞」在仅调用后处理 API（无 SampleSession 的 sim 旁路）时仍可能漏拦。SampleSession 用 `blockingIssue` 部分弥补，**不能**视为项 2 完成。须在 `FIX-EXPORT-GATE-002` 处理。
2. **NearCollision 扫掠短路**：当前 `postedSweepIssue=true` 后同段后续硬碰撞采样可被跳过——属项 2 范围，本项未改。
3. **UI 无 ConfigJump 开关**：关闭需改代码或后续加 checkbox；本项按计划接受。
4. **默认变严**：依赖旧「默认放行 Collision/ConfigJump」的外部脚本须显式关开关。
5. **项 3/4 未做**：UTF-8 路径加固、RISK-001 `.ls` 水印——计划串行后续项，不挡本项 APPROVED。

---

## 本轮范围内无 CHANGES_REQUESTED

无需修改点。仅当开 `FIX-EXPORT-GATE-002` 时实施扫掠/cache 回写。

---

# 审核结论 — BUGFIX-COMPILE-BAT-001 Round 1

> **Task ID**：`BUGFIX-COMPILE-BAT-001`
> **Task**：修复 Compile.bat 编译失败、start.bat 启动陷阱与构建脚本健壮性
> **Round**：1
> **Reviewer**：审核方
> **Reviewed at**：2026-10-28
> **Executor**：执行方
> **Verdict**：**APPROVED**
>
> 审核方已核对实际代码 diff：`src/algo/FclCollisionQueries.cpp` 中 `axisRotation` 已移出 `#ifdef OLP_HAS_FCL` 宏保护；`Tools/Compile.bat` 增加了 vcpkg 工具链及 Qt6_DIR 自动配置、`/p:VCToolsVersion=14.44.35207` MSBuild 参数、DLL 路径注入 `PATH` 以及 `--verify` 参数；`Tools/start.bat` 修正了产物优先级（优先使用 `build/Release`，解决改动后跑旧程序的暗坑），锚定了 `/D "%ROOT_DIR%"` 根工作目录，并注入了 `QT_PLUGIN_PATH`；根目录不增加多余脚本，保持清爽。两个 `.bat` 文件全量通过非 ASCII 字符扫描（0 命中，100% 英文合规）。`Tools/Compile.bat` 完整端到端编译并通过全量 752 项测试（退出码 0），`Tools/start.bat --verify` 运行通过（退出码 0）。本轮无 P0/P1 阻断项，审核通过。

---

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
