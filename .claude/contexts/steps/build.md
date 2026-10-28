# 构建与依赖

1. **算法回归与产品配置分开。** `OLP_HEADLESS=ON` 时不找 Qt 和 OpenCASCADE，用来跑运动学和平面场景。关掉该开关时，CMake 同时要求 Qt6 和 OpenCASCADE，缺一个就配置失败。`Tools/Compile.bat` 用延迟展开的 `!ERRORLEVEL!`，配置、编译、测试或主程序失败都返回非零。
2. **先装 OpenCASCADE，再装 Qt。** 用本机 `C:\vcpkg\vcpkg` 的经典模式安装 `opencascade:x64-windows`，得到 8.0.0（带 FreeType），前缀 `C:\vcpkg\vcpkg\installed\x64-windows`。不要在仓库目录里空跑 `vcpkg install`，仓库有 `vcpkg.json`，会进入清单模式并试图源码编译 Qt。vcpkg 自带 curl 连 GitHub 失败时，用系统 curl 的下载脚本 `C:\vcpkg\vcpkg\olp-download.ps1`（不在仓库里）通过 `X_VCPKG_ASSET_SOURCES` 代下。
3. **Qt 用官方安装器，不从源码编。** 装在 `D:\Soft\Qt\6.12.0\msvc2022_64`，只留 MSVC 2022 64 位。MinGW 与已经编好的 OCCT 不是同一套 ABI。
4. **产品构建目录是 `build-full`。** 生成器 Visual Studio 17 2022 x64。必须带 `-DVCPKG_MANIFEST_MODE=OFF`、vcpkg 工具链，以及 `Qt6_DIR` 指向上述套件的 `lib/cmake/Qt6`。运行前 PATH 要有 Qt 的 `bin` 和 vcpkg 的 `bin`。该目录已写入 `.gitignore`。
5. **FCL 用经典模式装在同一前缀。** `fcl 0.7.0` 由 vcpkg 用 MSVC 14.44 编译。这台机器的 v143 默认工具集是 14.38，直接链接会缺 `__std_max_element_f`。产品编译要带 `/p:VCToolsVersion=14.44.35207`。CMake 找到 FCL 时会打印 `Found FCL`。
