@echo off
setlocal enabledelayedexpansion

REM ==============================================================================
REM SiFastSuite OLP Application Startup Script
REM Sets up the runtime environment (Qt6 and OCCT DLL paths) and launches the GUI.
REM ==============================================================================

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."

REM Configure dependency search paths with environment variable overrides
if not defined QT_BIN (
    if exist "D:\Soft\Qt\6.12.0\msvc2022_64\bin" (
        set "QT_BIN=D:\Soft\Qt\6.12.0\msvc2022_64\bin"
    )
)
if not defined VCPKG_BIN (
    if exist "C:\vcpkg\vcpkg\installed\x64-windows\bin" (
        set "VCPKG_BIN=C:\vcpkg\vcpkg\installed\x64-windows\bin"
    )
)
if not defined QT_PLUGIN_PATH (
    if exist "D:\Soft\Qt\6.12.0\msvc2022_64\plugins" (
        set "QT_PLUGIN_PATH=D:\Soft\Qt\6.12.0\msvc2022_64\plugins"
    )
)

REM Prepend Qt and vcpkg binary directories to PATH
if defined QT_BIN (
    if exist "%QT_BIN%" (
        set "PATH=%QT_BIN%;!PATH!"
    )
)
if defined VCPKG_BIN (
    if exist "%VCPKG_BIN%" (
        set "PATH=%VCPKG_BIN%;!PATH!"
    )
)

REM Locate executable (prefer standard build output, fallback to build-full)
set "APP_EXE="
if exist "%ROOT_DIR%\build\Release\SiFastSuiteOLP.exe" (
    set "APP_EXE=%ROOT_DIR%\build\Release\SiFastSuiteOLP.exe"
) else if exist "%ROOT_DIR%\build-full\Release\SiFastSuiteOLP.exe" (
    set "APP_EXE=%ROOT_DIR%\build-full\Release\SiFastSuiteOLP.exe"
)

if not defined APP_EXE (
    echo [ERROR] SiFastSuiteOLP.exe was not found in build\Release or build-full\Release.
    echo Please build the project first using Tools\Compile.bat.
    exit /b 1
)

echo [INFO] Launching SiFastSuite OLP:
echo        !APP_EXE!

REM Check if running in verification mode
echo "%*" | findstr /i "\-\-verify" >nul
if !ERRORLEVEL! equ 0 (
    pushd "%ROOT_DIR%"
    "!APP_EXE!" %*
    set "RUN_EXIT=!ERRORLEVEL!"
    popd
    exit /b !RUN_EXIT!
) else (
    start "" /D "%ROOT_DIR%" "!APP_EXE!" %*
    exit /b 0
)
