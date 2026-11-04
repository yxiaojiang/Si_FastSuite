# Implementation Report — Task DIR-003-STEP1-PATH-001 (Round 1)

- **Task ID**: `DIR-003-STEP1-PATH-001`
- **Round**: `1`
- **Status**: `READY_FOR_REVIEW`
- **Executor**: Executor
- **Date**: 2026-11-04

---

## 1. 任务背景与目标（DIR-003 第 1 步：P0-Path）

按照 `PackagePlan.md` 与用户指令，推进 DIR-003 软件独立打包发布的「第 1 步（P0-Path，UTF-8 路径自适应与输出解耦）」：
1. **架构分层下沉**：将 `src/io/Utf8Path.cpp` 移入底层 `olp_core` 编译（保持 `include/olp/io/Utf8Path.h` 与 `olp::io` 命名空间不变），使 `olp_device`（Layer 4）无需反向链接 `olp_io`（Layer 5）即可直接使用路径工具；
2. **路径自适应函数**：在 `olp::io` 实现统一返回 UTF-8 `std::string` 的 `resolveWorkspaceOrAppDir()`（优先探测可执行文件同级的 `SampleScenes` 与 `resources`，不存在且处于开发环境时回退至 `OLP_SOURCE_DIR`）；同时提供 `pathToUtf8`、`isSandboxEnvironment` 与 `resolveSessionOutputDir`；
3. **源码路径解耦与中文路径支持**：
   - 改造 `src/device/RobotModel.cpp` 接入自适应函数，且第 107、142 行文件流改用 `olp::io::utf8ToPath` 打开，支持 Windows 中文路径；
   - 改造 `src/ui/SampleSession.cpp` 读取使用自适应函数，并将导出的 `WELD_001.ls` 与 `WELD_001_Preview.html` 在沙盒环境下写向用户临时目录 `std::filesystem::temp_directory_path() / "SiFastSuiteOLP"`，彻底消除 UAC 权限写保护与卸载残留隐患；
4. **测试补齐与验证**：补齐自动化单元测试，确保 `Tools/Compile.bat` 编译 exit 0、全量自动化单测全部通过、`--verify` exit 0，单文件行数严格处于安全区间。

---

## 2. 核心改动实施细节

### 2.1 架构分层下沉与 CMake 依赖解耦
- **`CMakeLists.txt`**：
  - 将 `src/io/Utf8Path.cpp` 从 `olp_io`（Layer 5）的目标源文件中移除，加入 `olp_core`（Layer 4）编译；
  - `olp_core` 供 `olp_device`、`olp_cad`、`olp_algo`、`olp_io` 全局依赖，`olp_device` 仅链接 `olp_core`，彻底杜绝逆向或循环依赖；
  - 头文件路径 `include/olp/io/Utf8Path.h` 与命名空间 `olp::io` 保持 100% 兼容。

### 2.2 UTF-8 路径自适应与沙盒探测机制
- **`include/olp/io/Utf8Path.h` 与 `src/io/Utf8Path.cpp`**：
  - `pathToUtf8(const std::filesystem::path& p)`：在 Windows 下使用 `generic_wstring()` 与 `WideCharToMultiByte(CP_UTF8)` 生成统一的 forward-slash UTF-8 字符串；非 Windows 下直接使用 `generic_u8string()`；
  - `isSandboxEnvironment()`：通过 `GetModuleFileNameW` 探测当前运行可执行文件同级目录，检查是否同时存在 `SampleScenes` 与 `resources` 目录。在打包发布的独立沙盒（`build/dist/`）中返回 `true`，在开发环境构建目录（`build/Release/`）中返回 `false`；
  - `resolveWorkspaceOrAppDir(const std::string& overrideRoot = "")`：
    1. 若传入 `overrideRoot` 则直接返回；
    2. 优先探测可执行文件同级目录是否包含 `SampleScenes` 与 `resources`，若存在直接以可执行文件所在目录为根；
    3. 若不存在且处于开发环境（宏定义 `OLP_SOURCE_DIR` 存在且包含 `SampleScenes`），平滑回退至源码根目录；
    4. 兜底回退至可执行文件所在目录或当前工作目录；
  - `resolveSessionOutputDir(const std::string& wsRoot = "", bool forceSandbox = false)`：
    - 若处于沙盒环境（或 `forceSandbox == true`），返回 `std::filesystem::temp_directory_path() / "SiFastSuiteOLP"` 并自动创建目录；
    - 若处于开发环境，返回 `<wsRoot>/build` 并自动创建目录。

### 2.3 源码绝对路径解耦与中文路径支持
- **`src/device/RobotModel.cpp`**：
  - 引入 `#include "olp/io/Utf8Path.h"`；
  - `RobotModel::createFanucM10iA`：通过 `workspaceRoot.empty() ? io::resolveWorkspaceOrAppDir() : workspaceRoot` 统一定位 `root`，网格目录 `baseDir` 与 JSON 路径 `jsonPath` 均基于 `root` 派生，彻底去除 `OLP_SOURCE_DIR` 宏硬编码；
  - 第 107 行读取 `fanuc_m10ia.json` 时，改用 `std::ifstream jin(io::utf8ToPath(jsonPath))`；
  - 第 142 行 `RobotModel::loadFromUrdf` 读取 URDF 文件时，改用 `std::ifstream ifs(io::utf8ToPath(urdfFilePath))`，彻底解决 Windows 中文路径打不开问题。
- **`src/ui/SampleSession.cpp`**：
  - 第 35 行将 `const std::string wsRoot = OLP_SOURCE_DIR;` 替换为 `const std::string wsRoot = olp::io::resolveWorkspaceOrAppDir();`；
  - 第 225-226 行输出路径改由 `olp::io::resolveSessionOutputDir(wsRoot)` 动态解析：
    - 沙盒安装环境下输出至 `%TEMP%/SiFastSuiteOLP/`，杜绝 `C:\Program Files` 下 UAC 写保护拒绝和卸载残留；
    - 源码开发环境下输出至 `<wsRoot>/build/`，保持与现有脚本/产物预期完全兼容。

### 2.4 补齐自动化单元测试
- **`tests/test_support.h`、`tests/test_main.cpp` 与 `tests/test_cad.cpp`**：
  - 新增 `void testUtf8PathAdaptationAndDecoupling()` 测试集，覆盖 15 项深度断言：
    1. `pathToUtf8` 与 `utf8ToPath` 空路径与中文字符串双向往返一致性；
    2. `resolveWorkspaceOrAppDir` 覆盖参数直接返回、无参自动解析开发目录、以及解析目录下 `SampleScenes` 和 `resources/robots/fanuc_m10ia.json` 真实存在性；
    3. `isSandboxEnvironment` 开发环境正确识别为 `false`；
    4. `resolveSessionOutputDir` 开发环境返回 `build/`、强制沙盒模式返回 `%TEMP%/SiFastSuiteOLP/`，并在临时目录执行物理文件写入读取权限校验；
    5. `RobotModel::createFanucM10iA` 无参调用自适应解析 JSON 并验证 J3/J6 偏置；
    6. `RobotModel::loadFromUrdf` 在临时中文路径下创建并成功解析 URDF 模型。

---

## 3. 修改文件与行数门禁清单

| 文件路径 | 职责与变更说明 | 修改后行数 | 门禁判定（< 1000 行软门禁） |
| :--- | :--- | :---: | :---: |
| `CMakeLists.txt` | 将 `Utf8Path.cpp` 从 `olp_io` 调整移入 `olp_core` 编译 | 225 | PASS（< 1000） |
| `include/olp/io/Utf8Path.h` | 声明 `pathToUtf8`、`isSandboxEnvironment`、`resolveWorkspaceOrAppDir`、`resolveSessionOutputDir` | 36 | PASS（< 1000） |
| `src/io/Utf8Path.cpp` | 实现 UTF-8 路径自适应解析与沙盒/开发输出重定向逻辑 | 136 | PASS（< 1000） |
| `src/device/RobotModel.cpp` | 接入 `resolveWorkspaceOrAppDir`，文件流使用 `utf8ToPath` | 296 | PASS（< 1000） |
| `src/ui/SampleSession.cpp` | 示例工位读取接入自适应，输出路径解耦为沙盒临时目录/开发 build | 272 | PASS（< 1000） |
| `tests/test_support.h` | 声明 `testUtf8PathAdaptationAndDecoupling` 测试函数 | 39 | PASS（< 1000） |
| `tests/test_main.cpp` | 测试主入口挂载新测试集 | 72 | PASS（< 1000） |
| `tests/test_cad.cpp` | 实现 15 项自适应与解耦自动化断言 | 555 | PASS（< 1000） |

---

## 4. 本机真实测试与验证结果

1. **编译构建**：
   - 执行 `Tools\Compile.bat`，MSVC 2022 Release 编译退出码 0，零警告零错误。
2. **自动化测试**：
   - `build/Testing/Temporary/LastTest.log` 显示全量测试：
     `Summary: 925 passed, 0 failed.`（由前置 910 项增至 925 项，全部 PASS）。
3. **主程序冒烟自检**：
   - `SiFastSuiteOLP.exe --verify` 运行退出码 0：
     - 正确加载 `Full_Welding_Cell_With_Robot.step`；
     - 识别并隐藏 5 个静态机器人实体，将 M-10iA 放置在底座 `(0, 0, 500)` mm；
     - 完成焊缝计算、轨迹生成、OPW 逆解烘焙 1047 帧；
     - 成功输出 FANUC `.ls` 程序与离线 HTML 3D 预览包。
