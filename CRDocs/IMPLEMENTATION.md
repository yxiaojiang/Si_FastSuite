# Implementation Report — Task DIR-003-STEP2-STAGING-001 (Round 1)

- **Task ID**: `DIR-003-STEP2-STAGING-001`
- **Round**: `1`
- **Status**: `READY_FOR_REVIEW`
- **Executor**: Executor
- **Date**: 2026-11-04

---

## 1. 任务背景与目标（DIR-003 第 2 步：P0-Staging）

按照 `PackagePlan.md` 与发布计划，推进 DIR-003 软件独立打包发布的「第 2 步（P0-Staging，31 个 vcpkg DLL + CRT 沙盒装配）」：
1. **纯英文装配脚本**：遵循脚本禁止包含中文硬约束，编写 `Tools/package_stage.bat`；
2. **沙盒环境装配（`build/dist/`）**：
   - 提取 Release 编译构建版主程序 `SiFastSuiteOLP.exe`；
   - 调用 `windeployqt` 抽取 Qt 运行时（平台插件、样式、图标、格式等），部署至 `plugins/`，生成 `qt.conf` 配置，主动剔除未引用的 `Qt6PrintSupport.dll`；
   - 收集拷贝全部 31 个 vcpkg 依赖库（24 个 OCCT 动态库 + `ccd.dll` + 6 个底层图形库）；
   - 收集拷贝 VS2022 官方 `Microsoft.VC143.CRT` 全套运行时（App-Local 模式）；
   - 完整展开 `SampleScenes/` 与 `resources/` 目录树；
3. **脱机运行验证**：在全新屏蔽外部 Qt/vcpkg `PATH` 的隔离终端中，运行 `build/dist/SiFastSuiteOLP.exe --verify`，验证其无需外部开发环境即可独立加载示例工位、执行逆解仿真并成功导出，且安装沙盒目录下零残留文件（导出写向临时目录）。

---

## 2. 核心改动实施细节

### 2.1 纯英文沙盒装配脚本 `Tools/package_stage.bat`
- **文件规范**：新建 `Tools/package_stage.bat`（219 行），严格遵循 100% 纯英文规范，无任何非 ASCII 字符或中文注释；
- **核心流程**：
  1. 自动化检测与探测 Qt、vcpkg、MSVC CRT 路径（兼容 VS2022 VC143 CRT 最新工具集）；
  2. 支持参数 `--skip-build`（复用现有 Release 构建）与 `--no-verify`（跳过冒烟验证）；
  3. 清空并重建 `build/dist/` 沙盒目录；
  4. 拷贝 `SiFastSuiteOLP.exe` 并调用 `windeployqt.exe`：
     `windeployqt.exe --release --no-compiler-runtime --no-translations --no-opengl-sw --no-printsupport --plugindir "%DIST_DIR%\plugins" "%DIST_DIR%\SiFastSuiteOLP.exe"`
  5. 落地 `qt.conf`：
     ```ini
     [Paths]
     Plugins = plugins
     ```
  6. 校验并拷贝全量 31 个 vcpkg 依赖 DLL；
  7. 拷贝 VS2022 VC143 CRT 运行时全量 DLL（`msvcp140.dll`、`vcruntime140.dll`、`vcruntime140_1.dll` 等 10 个 DLL）；
  8. 调用 `robocopy` 镜像展开 `SampleScenes/` 与 `resources/`；
  9. 将 `PATH` 彻底剥离外部开发环境（仅保留 `%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem`），执行 `build/dist/SiFastSuiteOLP.exe --verify`，并校验 `build/dist` 目录下没有遗留 `WELD_001.ls` 或 `WELD_001_Preview.html`。

### 2.2 31 个 vcpkg DLL 清单核验
通过对 `SiFastSuiteOLP.exe` 及其所调用模块的深度 PE 依赖递归追踪，31 个 DLL 均从 `C:\vcpkg\vcpkg\installed\x64-windows\bin` 完整收集并在 `build/dist/` 验证无误：
- **24 个 OCCT 动态库**：
  `TKBO.dll`, `TKBRep.dll`, `TKCAF.dll`, `TKCDF.dll`, `TKDE.dll`, `TKDESTEP.dll`, `TKG2d.dll`, `TKG3d.dll`, `TKGeomAlgo.dll`, `TKGeomBase.dll`, `TKHLR.dll`, `TKLCAF.dll`, `TKMath.dll`, `TKMesh.dll`, `TKOpenGl.dll`, `TKPrim.dll`, `TKService.dll`, `TKShHealing.dll`, `TKTopAlgo.dll`, `TKV3d.dll`, `TKVCAF.dll`, `TKXCAF.dll`, `TKXSBase.dll`, `TKernel.dll`
- **1 个碰撞几何算法库**：
  `ccd.dll`
- **6 个底层图形与压缩库**：
  `freetype.dll`, `libpng16.dll`, `z.dll`, `bz2.dll`, `brotlidec.dll`, `brotlicommon.dll`

---

## 3. 修改文件与行数门禁清单

| 文件路径 | 职责与变更说明 | 修改后行数 | 门禁判定（< 1000 行软门禁） |
| :--- | :--- | :---: | :---: |
| `Tools/package_stage.bat` | 纯英文沙盒装配与全隔离离线验证批处理脚本 | 219 | PASS（< 1000，纯英文） |

---

## 4. 本机真实测试与验证结果

1. **装配执行**：
   - 运行 `Tools\package_stage.bat --skip-build`，退出码 0：
     - `windeployqt` 成功提取核心 Qt 插件（`platforms/qwindows.dll`, `styles/`, `iconengines/`, `imageformats/` 等）；
     - 生成标准 `qt.conf`；
     - 31 个 vcpkg DLL 及 10 个 VC143 CRT DLL 100% 完整部署至 `build/dist/`；
     - `SampleScenes/`（1.23 MB）与 `resources/`（1.66 KB）完整展开。
2. **全隔离环境冒烟验证（`PATH` 无 Qt/vcpkg/VS）**：
   - 在将 `PATH` 严格限制为操作系统原生标准目录的隔离环境下，执行 `build/dist/SiFastSuiteOLP.exe --verify`，退出码为 0：
     - 成功解析并导入 `Full_Welding_Cell_With_Robot.step`（12 个 B-Rep 实体）；
     - 识别并隐藏 5 个静态实体，移动版 M-10iA 正确落座于底座 `(0, 0, 500)` mm；
     - 完成焊缝生成、轨迹规划与 OPW 运动学 1047 帧烘焙；
     - 导出的 `WELD_001.ls` 与 `WELD_001_Preview.html` 成功写入用户临时目录（`D:/UserTemp/SiFastSuiteOLP/`），`build/dist/` 目录下无任何临时文件残留。
