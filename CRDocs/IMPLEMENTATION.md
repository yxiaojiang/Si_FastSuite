# Implementation Report — Task DIR-003-STEP3-INSTALLER-001 (Round 1)

- **Task ID**: `DIR-003-STEP3-INSTALLER-001`
- **Round**: `1`
- **Status**: `READY_FOR_REVIEW`
- **Executor**: Executor
- **Date**: 2026-11-04

---

## 1. 任务背景与目标（DIR-003 第 3 步：P0-Installer）

按照 `PackagePlan.md` 与发布计划，推进 DIR-003 软件独立打包发布的「第 3 步（P0-Installer，Inno Setup 单文件安装包与验证）」：
1. **Inno Setup 脚本编写**：编写 `installer/SiFastSuite.iss`：
   - 默认安装位置设定为 `{autopf}\SiFastSuite OLP`（64 位 Windows 下默认 `C:\Program Files\SiFastSuite OLP`）；
   - 源映射：递归装配 `build/dist/*` 展开至 `{app}`；
   - 快捷方式：生成桌面图标、开始菜单快捷方式，并提供“打开示例模型目录”快捷入口；
   - 卸载器配置：注册标准卸载信息，确保卸载后安装目录完全移除无残留。
2. **纯英文统筹脚本**：编写 `Tools/Package.bat`，统筹调用 `package_stage.bat` 并调用 Inno Setup 命令行编译器 `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`，生成 `build/installer/SiFastSuiteOLP_Setup_v0.1.0.exe`；
3. **真实安装与卸载闭环实测**：执行真实安装、脱机环境运行与卸载测试，确认安装、脱机运行、卸载全生命周期闭环。

---

## 2. 核心改动实施细节

### 2.1 Inno Setup 配置文件 `installer/SiFastSuite.iss`
- **文件规模**：76 行，严格低于 1000 行软门禁；
- **配置要点**：
  - `AppId={{C6B5E86E-35F7-4180-87F8-E3E374E51F7D}}`：定义稳定唯一的 GUID；
  - `DefaultDirName={autopf}\SiFastSuite OLP`：64 位操作系统下默认指向 `C:\Program Files\SiFastSuite OLP`；
  - `ArchitecturesInstallIn64BitMode=x64compatible` 与 `ArchitecturesAllowed=x64compatible`：锁定 64 位平台架构；
  - `PrivilegesRequired=lowest` 与 `PrivilegesRequiredOverridesAllowed=dialog commandline`：向导模式下允许用户选择“为所有用户安装（需要管理员权限）”或“仅为当前用户安装”；自动化命令行模式下支持 `/CURRENTUSER` 无需提权直接测试；
  - `Compression=lzma2/ultra64` 与 `SolidCompression=yes`：高强度固实压缩，将 68 MB 运行沙盒压缩为 22.8 MB 单文件安装程序；
  - `[Files]`：`Source: "..\build\dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs` 完整映射沙盒；
  - `[Icons]`：生成主程序图标、示例模型目录快捷入口、桌面快捷方式任务与卸载器快捷方式；
  - `[UninstallDelete]`：指定卸载时清理 `plugins`、`SampleScenes`、`resources` 及临时可能写出的文件，并在空目录时删除 `{app}`，达成 100% 零残留。

### 2.2 纯英文统筹脚本 `Tools/Package.bat`
- **文件规范**：176 行，100% 纯英文，0 个非 ASCII 字符；
- **支持参数**：
  - `--skip-stage`：跳过沙盒组装（复用现有 `build/dist`）；
  - `--skip-build`：调用 `package_stage.bat` 时跳过重新编译；
  - `--no-verify`：调用 `package_stage.bat` 时跳过沙盒冒烟测试；
  - `--test-install`：编译安装包后，自动触发全套静默安装、脱机验证与静默卸载全生命周期测试；
- **核心逻辑**：
  1. 自动化检索 `ISCC.exe`（优先 `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`，兼容 PATH 查找）；
  2. 驱动 `package_stage.bat` 组装并验证最新沙盒；
  3. 执行 `ISCC.exe installer\SiFastSuite.iss` 编译输出；
  4. 当开启 `--test-install` 时，静默安装到 `build\test_install`，并在剥离外部 PATH 环境下调用 `build\test_install\SiFastSuiteOLP.exe --verify`，验证通过后调用 `unins000.exe` 静默卸载，检查目录被彻底清除。

---

## 3. 修改文件与行数门禁清单

| 文件路径 | 职责与变更说明 | 修改后行数 | 门禁判定（< 1000 行软门禁） |
| :--- | :--- | :---: | :---: |
| `installer/SiFastSuite.iss` | Inno Setup 6 单文件安装包制作脚本 | 76 | PASS（< 1000，纯英文） |
| `Tools/Package.bat` | 统筹沙盒组装、安装包编译与生命周期测试批处理脚本 | 176 | PASS（< 1000，纯英文） |

---

## 4. 本机真实测试与验证结果

1. **安装包编译**：
   - 运行 `Tools\Package.bat --skip-build --test-install`，退出码 0；
   - 成功生成单文件安装包：`build\installer\SiFastSuiteOLP_Setup_v0.1.0.exe`（文件大小 22,816,159 字节，约 22.8 MB）。
2. **静默安装验证**：
   - 安装程序通过命令行参数 `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /DIR="build\test_install"` 成功展开；
   - 验证 `build\test_install\` 下主程序、`qt.conf`、`plugins/` 目录树、31 个 vcpkg DLL、10 个 VC143 CRT DLL、`SampleScenes/` 与 `resources/` 100% 完整展开。
3. **已安装程序脱机验证**：
   - 在将 `PATH` 严格重置为仅含 `%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem` 的无开发环境隔离终端中，运行：
     `build\test_install\SiFastSuiteOLP.exe --verify`
   - **退出码 0**：成功载入示例工位 STEP、识别 12 个 B-Rep 实体、隐藏 5 个静态实体、移动版 M-10iA 落在底座 `(0, 0, 500)` mm、烘焙 1047 帧逆解与轨迹，导出的 `.ls` 与 HTML 成功写向用户临时目录 `D:/UserTemp/SiFastSuiteOLP/`，安装目录内无任何临时文件残留。
4. **静默卸载验证**：
   - 执行 `build\test_install\unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`；
   - 等待卸载完成，检测 `build\test_install` 下可执行文件与目录完全移除，无残留文件。
5. **构建与全量测试回归**：
   - 运行 `Tools\Compile.bat`，构建成功，全量自动化单元测试保持 **925 passed / 0 failed**，`--verify` exit 0。
