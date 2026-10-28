# BUGFIX-COMPILE-BAT-001 — Round 1：修复 Compile.bat 编译失败、start.bat 启动陷阱与构建脚本健壮性

> 执行者：执行方（Executor）
>
> 日期：2026-10-28
>
> 轮次：Round 1
>
> 状态：**READY_FOR_REVIEW**

响应用户关于“修复 Compile.bat 无法编译通过及排查 start.bat”的指令，执行方已全面排查、修复并端到端验证构建脚本、启动脚本与相关底层编译问题，并保持根目录干净，脚本归口 `Tools/`。

---

## 一、 问题定位与根本原因分析

1. **`Tools\Compile.bat` 未配置 CMake 工具链与 Qt 路径**：
   - 原脚本在初次生成 `build\` 时直接调用 `cmake -B build -S .`，未传入 `-DCMAKE_TOOLCHAIN_FILE` 与 `-DQt6_DIR`，且未指定 `-DVCPKG_MANIFEST_MODE=OFF`；
   - 导致 CMake 生成的缓存中 `nlohmann_json`、`Eigen3`、`OpenCASCADE` 等包全部丢失，在编译 `ProjectSerializer.cpp` 时报 `error C1083: 无法打开包括文件: “nlohmann/json.hpp”: No such file or directory`；
   - 且 CMake 一旦生成无工具链的缓存，后续即使重传变量也无法覆盖，导致 `build\` 目录持久处于失效状态。
2. **未传递 MSVC 工具集锁定参数**：
   - 原脚本仅执行 `cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel`，未传递硬约束要求的 `/p:VCToolsVersion=14.44.35207`。
3. **运行时动态库缺失（0xc0000135）**：
   - 原脚本运行 `ctest` 与生成的可执行程序前，未在 `PATH` 中前置注入 Qt6 bin（`D:\Soft\Qt\6.12.0\msvc2022_64\bin`）与 vcpkg bin（`C:\vcpkg\vcpkg\installed\x64-windows\bin`），导致 ctest 执行测试时报 `0xc0000135` (STATUS_DLL_NOT_FOUND) 失败。
4. **GUI 模式阻塞控制台执行**：
   - 原脚本在行 76 直接调用 `SiFastSuiteOLP.exe` 而未带 `--verify` 参数，直接进入 Qt GUI 事件循环（`app.exec()`），导致在非交互控制台构建脚本中永久阻塞。
5. **`FclCollisionQueries.cpp` 条件宏范围缺陷**：
   - `axisRotation` 辅助旋转矩阵计算函数原被包在 `#ifdef OLP_HAS_FCL` 宏内，但无宏保护的 `forwardRobotLinkWorldFrames` 仍对其调用，导致在无 FCL 或非 FCL 配置下报 `error C3861: “axisRotation”: 找不到标识符`。
6. **`Tools\start.bat` 存在 4 大隐患与暗坑**：
   - **产物搜索优先级倒置**：原脚本优先检查 `build-full`，其次才是 `build`。当使用 `Compile.bat` 重新编译更新 `build\Release` 后，执行 `start.bat` 仍会无条件运行旧版 `build-full` 中的陈旧二进制，造成“代码改了却没生效”的假象；
   - **工作目录未锚定**：原脚本执行 `start "" "!APP_EXE!" %*` 时未指定 `/D "%ROOT_DIR%"`。若在 `Tools/` 目录下运行，进程当前目录为 `Tools/`，导致相对路径资源（如 `./SampleScenes`）寻找失败；
   - **缺少 `QT_PLUGIN_PATH` 环境变量兜底**：缺少对平台插件路径的显式配置；
   - **缺少外部环境变量覆盖**：未对 `QT_BIN`、`VCPKG_BIN` 等环境变量预留覆盖支持。

---

## 二、 实际修改内容

### 1. 修复 `src/algo/FclCollisionQueries.cpp`
- 将 `axisRotation(const core::Vec3& axis, double angle)` 函数移出 `#ifdef OLP_HAS_FCL` 保护块，置于匿名命名空间顶层；
- 保证无论是否启用 FCL，`forwardRobotLinkWorldFrames` 与各类坐标系正向变换均可正常编译。

### 2. 重构与增强 `Tools/Compile.bat`
- **自动适配环境**：自动探测并设置 `QT6_DIR`、`QT_BIN`、`VCPKG_TOOLCHAIN`、`VCPKG_BIN`（支持环境变量覆盖）；
- **PATH 注入**：构建及测试前自动将 Qt 与 vcpkg 的 `bin` 路径注入 `PATH`，解决运行期 DLL 加载失败；
- **失效缓存自愈**：自动检测 `CMakeCache.txt` 是否包含有效的 `CMAKE_TOOLCHAIN_FILE`，若缺失或损坏自动清理旧缓存并以完整工具链重新配置；
- **标准编译参数**：严格附带 `/p:VCToolsVersion=14.44.35207`；
- **验证命令修补**：将 CLI 验证调用统一为 `SiFastSuiteOLP.exe --verify`，确保快速验证退出码为 0，不挂死在 GUI 循环；
- **参数拓展支持**：支持 `Release` / `Debug` 配置切换，以及 `--clean`、`--reconfigure`、`--no-tests` 标志；
- **纯英文合规**：严格遵守 `Rules.md` 脚本纯英文规范，无任何中文字符。

### 3. 重构与加固 `Tools/start.bat`
- **修正产物优先级**：优先启动标准构建输出 `build\Release\SiFastSuiteOLP.exe`，回退检查 `build-full\Release\SiFastSuiteOLP.exe`；
- **显式锚定工作目录**：以 `start "" /D "%ROOT_DIR%" "!APP_EXE!" %*` 启动 GUI，验证模式以 `pushd "%ROOT_DIR%"` 运行，确保资源与相对路径永远基于项目根目录解析；
- **依赖与插件加固**：增加 `QT_PLUGIN_PATH` 注入与外部环境变量支持；
- **纯英文合规**：100% 英文注释与输出。

---

## 三、 风险与约束遵守情况

1. **单文件行数限制（1000 行）**：
   - `src/algo/FclCollisionQueries.cpp`: 422 行（$< 1000$）；
   - `Tools/Compile.bat`: 180 行（$< 1000$）；
   - `Tools/start.bat`: 72 行（$< 1000$）；
   - `CRDocs/STATUS.md`: 33 行（$< 1000$）。
2. **纯英文规范**：`Tools/start.bat` 与 `Tools/Compile.bat` 经非 ASCII 字符扫描，结果为 0 处命中，100% 英文。
3. **未改变核心主线状态**：`RISK-001`、`RISK-002` 等继续按既定规则保持 OPEN。

---

## 四、 验证证据

### 1. `Tools\Compile.bat` 构建与全量测试验证
```cmd
cmd.exe /c "Tools\Compile.bat"
```
**实际输出结果**：
- `[SiFastSuite Build] Valid CMake configuration detected.`
- `[SiFastSuite Build] Building project targets (Release)...`（MSBuild `/p:VCToolsVersion=14.44.35207` 编译全部目标完成，退出码 0）
- `[SiFastSuite Build] Running verification tests...`（CTest 100% tests passed, 0 tests failed out of 1，覆盖 752 项全量自动化测试）
- `[SiFastSuite Build] Executing SiFastSuiteOLP verification run...`（`SiFastSuiteOLP.exe --verify` 运行 1/5~5/5 步骤全部完成，退出码 0）
- `[SiFastSuite Build] Build and verification finished successfully.`
- 退出码：**0**。

### 2. `Tools\start.bat` 启动与验证
```cmd
cmd.exe /c "Tools\start.bat --verify"
```
**实际输出结果**：
- 正确命中并加载 `build\Release\SiFastSuiteOLP.exe`（而非陈旧的 build-full）；
- 五步业务流程全部通过，退出码：**0**。
- GUI 模式启动测试：无崩溃，主窗口及事件循环正常稳定运行。

---

# M15-TJOINT-REGRESSION-001 — Round 1：结束 3D PDF 悬置（收口 RISK-003）与 SampleScenes T 型接头闭环自动化回归测试（DIR-001 第 7 步，M13/M15）

> 执行者：执行方（Executor）
>
> 日期：2026-10-28
>
> 轮次：Round 1
>
> 状态：**APPROVED**
