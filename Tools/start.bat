@echo off
setlocal enabledelayedexpansion

REM ==============================================================================
REM SiFastSuite OLP Application Startup Script
REM Sets up the runtime environment (Qt6 and OCCT DLL paths) and launches the GUI.
REM ==============================================================================

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."

REM Configure default dependency search paths
set "QT_BIN_DEFAULT=D:\Soft\Qt\6.12.0\msvc2022_64\bin"
set "VCPKG_BIN_DEFAULT=C:\vcpkg\vcpkg\installed\x64-windows\bin"

REM Append Qt binary directory if present
if exist "%QT_BIN_DEFAULT%" (
    set "PATH=%QT_BIN_DEFAULT%;!PATH!"
)

REM Append vcpkg binary directory if present
if exist "%VCPKG_BIN_DEFAULT%" (
    set "PATH=%VCPKG_BIN_DEFAULT%;!PATH!"
)

REM Locate the executable (prefer build-full, fallback to build)
set "APP_EXE="
if exist "%ROOT_DIR%\build-full\Release\SiFastSuiteOLP.exe" (
    set "APP_EXE=%ROOT_DIR%\build-full\Release\SiFastSuiteOLP.exe"
) else if exist "%ROOT_DIR%\build\Release\SiFastSuiteOLP.exe" (
    set "APP_EXE=%ROOT_DIR%\build\Release\SiFastSuiteOLP.exe"
)

if not defined APP_EXE (
    echo [ERROR] SiFastSuiteOLP.exe was not found in build-full\Release or build\Release.
    echo Please build the project first before running this script.
    exit /b 1
)

echo [INFO] Launching SiFastSuite OLP:
echo        !APP_EXE!

REM Check if running in verification mode
echo "%*" | findstr /i "\-\-verify" >nul
if !ERRORLEVEL! equ 0 (
    "!APP_EXE!" %*
    exit /b !ERRORLEVEL!
) else (
    start "" "!APP_EXE!" %*
    exit /b 0
)
