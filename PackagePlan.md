# SiFastSuite OLP 软件独立打包与安装包发布实施方案（PackagePlan）

> **文档标识**：`DIR-003-PLAN`  
> **关联讨论**：[PlanDocs/PROPOSAL.md](file:///C:/Users/mimovr/Desktop/TEmp/Si_FastSuite/PlanDocs/PROPOSAL.md)（Round 2）  
> **当前状态**：`READY_FOR_REVIEW`（待 Argon 第 2 轮审核）  
> **修订日期**：2026-11-04  

---

## 1. 目标与需求背景

### 1.1 现状与痛点
目前本工程通过 [`Tools/start.bat`](file:///C:/Users/mimovr/Desktop/TEmp/Si_FastSuite/Tools/start.bat) 启动，强依赖当前开发机器预设的环境变量：
- Qt 6.12.0 MSVC2022_64：`D:\Soft\Qt\6.12.0\msvc2022_64\bin` 及插件目录；
- OpenCASCADE 8.0.0 与三方算法库：`C:\vcpkg\vcpkg\installed\x64-windows\bin`；
- 源码硬编码：源码中直接读取编译期宏 `OLP_SOURCE_DIR` 获取 `SampleScenes` 与 `resources` 资源；且示例会话硬编码向只读安装目录写出文件。

若将程序直接拷贝到无 Qt/vcpkg/VS 开发环境的目标客户机，软件会因**缺少底层动态库（含二阶传递依赖）、缺少 Qt 平台插件、缺少运行时、找不到示例文件或 UAC 写入权限被拒**而无法正常运行。

### 1.2 目标交付形态
- **交付介质**：单文件安装程序 `SiFastSuiteOLP_Setup_v0.1.0.exe`。
- **安装行为**：
  1. 运行安装向导，允许用户指定安装路径（默认 `C:\Program Files\SiFastSuite OLP\`）；
  2. 将主程序、全套运行时依赖 DLL（31 个 vcpkg DLL + MSVC CRT）、Qt 平台插件及附带的 `SampleScenes` 完整**展开到安装目录下**；
  3. 创建桌面快捷方式与开始菜单项；
  4. 注册 Windows 标准卸载信息（支持在“控制面板/已安装应用”中干净卸载，无残留文件）。

---

## 2. 依赖审计与沙盒结构设计

### 2.1 运行时依赖审计（经真实递归扫描确证）
通过对 `build/Release/SiFastSuiteOLP.exe` 及其依赖项的递归扫描，系统脱机运行必须包含以下完整依赖项：

| 依赖类别 | 组件/模块 | 数量/来源 | 详细说明 |
| :--- | :--- | :--- | :--- |
| **主程序** | `SiFastSuiteOLP.exe` | 1 个 | Release 编译优化版 |
| **Qt 6 运行时** | `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll` | 3 个 | 来自 `D:\Soft\Qt\6.12.0\msvc2022_64\bin`（`Qt6PrintSupport.dll` 未被主程序引用，已按链接器 `/OPT:REF` 剔除） |
| **Qt 平台插件** | `plugins/platforms/qwindows.dll`, `styles/`, `iconengines/`, `imageformats/` | 目录树 | 来自 Qt 插件目录，由 `windeployqt` 自动部署 |
| **环境隔离配置** | `qt.conf` | 1 个 | 内容 `[Paths] Plugins=plugins`，阻断全局 Qt 干扰 |
| **vcpkg 依赖库** | 24 个 `TK*.dll` + `ccd.dll` + 6 个底层图形支持库 | **共 31 个** | 包含 16 个一阶 OCCT 库、8 个二阶 OCCT 库（`TKBO`, `TKCAF`, `TKCDF`, `TKDE`, `TKG2d`, `TKGeomAlgo`, `TKHLR`, `TKVCAF`）及 6 个支撑库（`freetype.dll`, `libpng16.dll`, `z.dll`, `bz2.dll`, `brotlidec.dll`, `brotlicommon.dll`） |
| **MSVC CRT 运行时** | `msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` 等 | 完整套件 | 采用官方 App-Local 方式，直接从 `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT` 提取部署 |
| **示例与数据** | `SampleScenes/`（约 1.23 MB）、`resources/`（约 1.66 KB） | 目录树 | 完整镜像展开 |

### 2.2 发布沙盒目录结构规范（`build/dist/`）
在打包制作 exe 前，构建系统将先装配出一套完全独立的免安装运行沙盒：
```text
build/dist/
├── SiFastSuiteOLP.exe                  # 编译优化版可执行文件
├── qt.conf                             # 声明本地插件路径，阻断外部 Qt 污染
├── Qt6Core.dll
├── Qt6Gui.dll
├── Qt6Widgets.dll
├── [31 个 vcpkg 动态链接库]
│   ├── TKernel.dll, TKBRep.dll, TKDESTEP.dll, TKOpenGl.dll ... (24个OCCT库)
│   ├── ccd.dll
│   └── freetype.dll, libpng16.dll, z.dll, bz2.dll, brotlidec.dll, brotlicommon.dll
├── [MSVC VC143 CRT 运行时 DLL]
│   ├── msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll ...
├── plugins/
│   ├── platforms/
│   │   └── qwindows.dll                # Windows 平台核心显示插件
│   ├── iconengines/
│   ├── imageformats/
│   └── styles/
├── resources/
│   └── robots/
│       └── fanuc_m10ia.json
└── SampleScenes/                       # 随安装包展开的示例数据
    ├── Scene1_TJoint_And_FANUC_M10iA/
    └── Scene2_Full_Workcell_STEP/
```

---

## 3. 关键技术改造点

### 3.1 架构分层合规与下沉
- **现状**：`CMakeLists.txt:89-95` 中 `olp_device`（Layer 4）仅链接 `olp_core`；而 `olp_io`（Layer 5）依赖 `olp_device`。`src/io/Utf8Path.cpp` 当前属于 `olp_io`。
- **改造**：将 `src/io/Utf8Path.cpp` 编译下沉至底层 `olp_core`（头文件 `include/olp/io/Utf8Path.h` 与 `olp::io` 命名空间保持不变）。使 `olp_device`、`olp_cad` 均可直接调用路径工具，严禁出现逆向或循环依赖。

### 3.2 源码路径自适应与 UTF-8 编码契约
- **现状**：`src/device/RobotModel.cpp:93, 105` 与 `src/ui/SampleSession.cpp:35` 直接写死 `OLP_SOURCE_DIR`；且 `RobotModel.cpp` 直接用窄字符 `std::string` 初始化 `std::ifstream`，在 Windows 中文系统下导致中文路径乱码崩溃。
- **改造**：
  1. 在 `olp::io` 实现统一返回 **UTF-8 `std::string`** 的 `resolveWorkspaceOrAppDir()`：
     - 若可执行程序同级存在 `SampleScenes` 与 `resources`，直接以可执行文件所在目录为根；
     - 若不存在且宏定义 `OLP_SOURCE_DIR` 有效，平滑回退至源码根目录；
  2. 改造 `RobotModel.cpp:107, 142` 的 `std::ifstream`，统一使用 `olp::io::utf8ToPath` 打开。

### 3.3 示例会话输出目录解耦（解决 UAC 权限与卸载残留）
- **现状**：`src/ui/SampleSession.cpp:225-253` 硬编码向 `wsRoot + "/build/WELD_001.ls"` 与 `WELD_001_Preview.html` 写入，且失败直接判定整个会话失败（`exitCode = 1`）。在 `C:\Program Files` 下受 UAC 写保护必然失败，且在安装目录下生成新文件会导致卸载不干净。
- **改造**：
  - 将“只读资源读取”与“运行时输出”彻底分离；
  - 读取 `SampleScenes/` 继续使用 `resolveWorkspaceOrAppDir()`；
  - 自动导出的 `WELD_001.ls` 与 `WELD_001_Preview.html`，在安装沙盒环境下写向用户临时目录（`std::filesystem::temp_directory_path() / "SiFastSuiteOLP"`），在源码开发环境下仍保留 `wsRoot + "/build/..."`。

---

## 4. 实施顺序与分步路线图

严格按照如下顺序推进：

### 第 0 步：前置代码基线收口（前置门禁）
- 先行在 `CRDocs` 中解决当前 `CHANGES_REQUESTED` 的 `DIR-002-STEP-IMPORT-001` 任务（修复 `DIR-002-001` 与 `DIR-002-002`）；
- 审核通过并变更为 `APPROVED` 后，再行启动 `DIR-003` 的代码改动，确保基线纯净。

### 第 1 步：UTF-8 路径自适应与会话输出解耦（P0-Path）
- 在 `CMakeLists.txt` 中将 `src/io/Utf8Path.cpp` 调整为 `olp_core` 编译；
- 实现 `resolveWorkspaceOrAppDir()` 返回 UTF-8 字符串；
- 改造 `RobotModel.cpp`（接入路径自适应并修复 `std::ifstream` 中文路径）与 `SampleSession.cpp`（只读路径与可写临时路径解耦）；
- 补齐单元测试，确保 `Tools/Compile.bat` 全量测试全绿。

### 第 2 步：31 个 vcpkg DLL + CRT 沙盒完整装配（P0-Staging）
- 编写遵循纯英文规范的批处理脚本 `Tools/package_stage.bat`：
  1. 调用 CMake 执行干净 Release 构建；
  2. 调用 `windeployqt` 抽取 Qt 运行时（剔除 `Qt6PrintSupport`），生成 `qt.conf`；
  3. 拷贝全部 31 个 vcpkg DLL（24 个 OCCT 动态库 + `ccd.dll` + 6 个底层图形库）；
  4. 拷贝 VS2022 官方 `Microsoft.VC143.CRT` 运行时；
  5. 完整镜像拷贝 `SampleScenes/` 与 `resources/`；
- 在屏蔽外部 Qt/vcpkg `PATH` 的全新终端中运行 `build/dist/SiFastSuiteOLP.exe --verify`，验证离线运行与示例加载。

### 第 3 步：Inno Setup 单文件安装包输出与安装/卸载验证（P0-Installer）
- 编写 Inno Setup 配置文件 `installer/SiFastSuite.iss`：
  - 默认安装位置：`{autopf}\SiFastSuite OLP`；
  - 源目录映射：递归装配 `build/dist/*` 展开至 `{app}`；
  - 快捷方式：生成桌面图标与开始菜单项，并提供“打开示例模型目录”快捷入口；
  - 卸载器配置：确保无残留文件。
- 编写纯英文 `Tools/Package.bat`，统筹执行装配并调用 `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`，生成 `build/installer/SiFastSuiteOLP_Setup_v0.1.0.exe`。
- 执行真实安装与卸载测试，确认安装、运行、卸载全生命周期闭环。

---

## 5. 验收标准（Acceptance Criteria）

1. **构建与沙盒验证**：
   - `Tools/Compile.bat` 零警告零错误，全量自动化单测全部通过；
   - 在完全清空外部 Qt/vcpkg 环境变量的终端下，`build/dist/SiFastSuiteOLP.exe --verify` 运行返回 0，无任何缺失 DLL 报错弹窗。
2. **安装程序验证**：
   - 成功生成单文件 `SiFastSuiteOLP_Setup_v0.1.0.exe`；
   - 在独立路径下安装后，主程序、全套依赖库及 `SampleScenes/` 完整展开；
   - 点击桌面快捷方式启动无异常；点击主界面「加载示例工位」能顺利导入 STEP、进行仿真，且生成的 `.ls` 与 HTML 导出文件正确写入临时目录，无 UAC 报错弹窗。
3. **卸载验证**：
   - 通过 Windows 设置/控制面板卸载程序，安装目录与快捷方式被 100% 完整移除，无残留文件。
