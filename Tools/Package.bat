@echo off
setlocal enabledelayedexpansion

REM ==============================================================================
REM SiFastSuite OLP Master Standalone Packaging Script
REM Orchestrates sandbox staging and Inno Setup 6 compilation to produce
REM the standalone installer: build\installer\SiFastSuiteOLP_Setup_v0.1.0.exe
REM ==============================================================================

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build"
set "DIST_DIR=%BUILD_DIR%\dist"
set "INSTALLER_DIR=%BUILD_DIR%\installer"
set "SETUP_EXE=%INSTALLER_DIR%\SiFastSuiteOLP_Setup_v0.1.0.exe"

set "DO_STAGE=1"
set "STAGE_ARGS="
set "DO_TEST_INSTALL=0"

REM Parse arguments
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--skip-stage" (
    set "DO_STAGE=0"
    shift
    goto parse_args
)
if /i "%~1"=="--skip-build" (
    set "STAGE_ARGS=!STAGE_ARGS! --skip-build"
    shift
    goto parse_args
)
if /i "%~1"=="--no-verify" (
    set "STAGE_ARGS=!STAGE_ARGS! --no-verify"
    shift
    goto parse_args
)
if /i "%~1"=="--test-install" (
    set "DO_TEST_INSTALL=1"
    shift
    goto parse_args
)
shift
goto parse_args
:args_done

echo ==============================================================================
echo [SiFastSuite Package] Starting Standalone Installer Packaging Process
echo [SiFastSuite Package] Project Root : %ROOT_DIR%
echo [SiFastSuite Package] Output Folder: %INSTALLER_DIR%
echo ==============================================================================

REM Check for Inno Setup compiler
set "ISCC_EXE="
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" set "ISCC_EXE=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not defined ISCC_EXE (
    where iscc >nul 2>nul
    if !ERRORLEVEL! equ 0 (
        for /f "tokens=*" %%I in ('where iscc') do (
            if not defined ISCC_EXE set "ISCC_EXE=%%I"
        )
    )
)

if not defined ISCC_EXE (
    echo [ERROR] Inno Setup 6 compiler ISCC.exe was not found.
    echo Please install Inno Setup 6 or add it to PATH.
    exit /b 1
)

echo [SiFastSuite Package] Found Inno Setup Compiler:
echo        !ISCC_EXE!

REM Run staging script to ensure build\dist is complete and verified
if "%DO_STAGE%"=="1" (
    echo [SiFastSuite Package] Assembling sandbox staging directory...
    call "%SCRIPT_DIR%package_stage.bat" !STAGE_ARGS!
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] package_stage.bat failed with code !ERRORLEVEL!.
        exit /b 1
    )
) else (
    if not exist "%DIST_DIR%\SiFastSuiteOLP.exe" (
        echo [ERROR] Staging directory %DIST_DIR% is incomplete. Cannot skip staging.
        exit /b 1
    )
)

REM Ensure output directory exists
if not exist "%INSTALLER_DIR%" (
    mkdir "%INSTALLER_DIR%"
)

REM Compile installer script
echo [SiFastSuite Package] Compiling installer with Inno Setup...
"!ISCC_EXE!" "%ROOT_DIR%\installer\SiFastSuite.iss"
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Inno Setup compilation failed with code !ERRORLEVEL!.
    exit /b 1
)

if not exist "%SETUP_EXE%" (
    echo [ERROR] Expected installer binary not found: %SETUP_EXE%
    exit /b 1
)

echo ==============================================================================
echo [SiFastSuite Package] Single-file installer generated successfully:
echo        %SETUP_EXE%
echo ==============================================================================

REM Optional verification of silent installation and uninstallation
if "%DO_TEST_INSTALL%"=="1" (
    echo [SiFastSuite Package] Executing test installation and verification...
    set "TEST_INSTALL_DIR=%BUILD_DIR%\test_install"
    
    if exist "!TEST_INSTALL_DIR!" (
        rmdir /s /q "!TEST_INSTALL_DIR!" >nul 2>nul
    )
    
    echo [SiFastSuite Package] Installing silently to !TEST_INSTALL_DIR!...
    "%SETUP_EXE%" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /DIR="!TEST_INSTALL_DIR!"
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] Silent installation failed with code !ERRORLEVEL!.
        exit /b 1
    )
    
    if not exist "!TEST_INSTALL_DIR!\SiFastSuiteOLP.exe" (
        echo [ERROR] Installed SiFastSuiteOLP.exe missing in !TEST_INSTALL_DIR!.
        exit /b 1
    )
    if not exist "!TEST_INSTALL_DIR!\qt.conf" (
        echo [ERROR] Installed qt.conf missing in !TEST_INSTALL_DIR!.
        exit /b 1
    )
    if not exist "!TEST_INSTALL_DIR!\SampleScenes" (
        echo [ERROR] Installed SampleScenes directory missing in !TEST_INSTALL_DIR!.
        exit /b 1
    )

    echo [SiFastSuite Package] Running isolated verification on installed app...
    set "OLD_PATH=%PATH%"
    set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem"
    
    "!TEST_INSTALL_DIR!\SiFastSuiteOLP.exe" --verify
    set "RUN_EXIT=!ERRORLEVEL!"
    set "PATH=!OLD_PATH!"
    
    if !RUN_EXIT! neq 0 (
        echo [ERROR] Installed app verification failed with exit code !RUN_EXIT!.
        exit /b !RUN_EXIT!
    )
    
    echo [SiFastSuite Package] Uninstalling silently...
    "!TEST_INSTALL_DIR!\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
    
    REM Wait for uninstaller process to complete
    ping -n 4 127.0.0.1 >nul
    
    REM Check that executable and plugins are removed
    if exist "!TEST_INSTALL_DIR!\SiFastSuiteOLP.exe" (
        echo [ERROR] SiFastSuiteOLP.exe still exists after uninstallation.
        exit /b 1
    )
    if exist "!TEST_INSTALL_DIR!" (
        rmdir /s /q "!TEST_INSTALL_DIR!" >nul 2>nul
    )
    
    echo [SiFastSuite Package] Test installation, execution, and uninstallation passed cleanly.
)

echo ==============================================================================
echo [SiFastSuite Package] Standalone package build completed successfully.
echo ==============================================================================
exit /b 0
