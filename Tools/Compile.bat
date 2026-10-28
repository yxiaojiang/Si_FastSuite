@echo off
setlocal enabledelayedexpansion

REM ==============================================================================
REM SiFastSuite OLP Build and Verification Script
REM Configures, builds, and verifies the project using CMake and Visual Studio 2022.
REM ==============================================================================

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build"
set "CONFIG=Release"
set "DO_CLEAN=0"
set "DO_RECONFIG=0"
set "DO_TESTS=1"

REM Parse optional arguments
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="Release" (
    set "CONFIG=Release"
    shift
    goto parse_args
)
if /i "%~1"=="Debug" (
    set "CONFIG=Debug"
    shift
    goto parse_args
)
if /i "%~1"=="--clean" (
    set "DO_CLEAN=1"
    shift
    goto parse_args
)
if /i "%~1"=="--reconfigure" (
    set "DO_RECONFIG=1"
    shift
    goto parse_args
)
if /i "%~1"=="--no-tests" (
    set "DO_TESTS=0"
    shift
    goto parse_args
)
shift
goto parse_args
:args_done

echo ==============================================================================
echo [SiFastSuite Build] Starting Build Process
echo [SiFastSuite Build] Project Root : %ROOT_DIR%
echo [SiFastSuite Build] Build Folder : %BUILD_DIR%
echo [SiFastSuite Build] Configuration: %CONFIG%
echo ==============================================================================

REM Check if CMake is installed and available in PATH
where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake is not found in PATH.
    echo Please install CMake or add its bin directory to PATH.
    exit /b 1
)

REM Setup default dependency paths
if not defined QT6_DIR (
    if exist "D:\Soft\Qt\6.12.0\msvc2022_64\lib\cmake\Qt6" (
        set "QT6_DIR=D:\Soft\Qt\6.12.0\msvc2022_64\lib\cmake\Qt6"
    )
)
if not defined QT_BIN (
    if exist "D:\Soft\Qt\6.12.0\msvc2022_64\bin" (
        set "QT_BIN=D:\Soft\Qt\6.12.0\msvc2022_64\bin"
    )
)
if not defined VCPKG_TOOLCHAIN (
    if exist "C:\vcpkg\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_TOOLCHAIN=C:\vcpkg\vcpkg\scripts\buildsystems\vcpkg.cmake"
    )
)
if not defined VCPKG_BIN (
    if exist "C:\vcpkg\vcpkg\installed\x64-windows\bin" (
        set "VCPKG_BIN=C:\vcpkg\vcpkg\installed\x64-windows\bin"
    )
)

REM Prepend dependency binaries to PATH so tests and executables resolve runtime DLLs
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

REM Clean build directory if requested
if "%DO_CLEAN%"=="1" (
    echo [SiFastSuite Build] Cleaning build directory...
    if exist "%BUILD_DIR%" (
        rmdir /s /q "%BUILD_DIR%" >nul 2>nul
    )
)

REM Create build directory if needed
if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
)

REM Check whether existing cache is stale or missing toolchain configuration
set "NEED_CONFIG=0"
if "%DO_RECONFIG%"=="1" (
    set "NEED_CONFIG=1"
) else if not exist "%BUILD_DIR%\CMakeCache.txt" (
    set "NEED_CONFIG=1"
) else (
    findstr /i "CMAKE_TOOLCHAIN_FILE" "%BUILD_DIR%\CMakeCache.txt" >nul 2>nul
    if !ERRORLEVEL! neq 0 (
        echo [SiFastSuite Build] Existing cache is missing vcpkg toolchain. Reconfiguring...
        set "NEED_CONFIG=1"
    )
)

if "%NEED_CONFIG%"=="1" (
    echo [SiFastSuite Build] Configuring project with CMake...
    if exist "%BUILD_DIR%\CMakeCache.txt" (
        del /f /q "%BUILD_DIR%\CMakeCache.txt" >nul 2>nul
    )
    if exist "%BUILD_DIR%\CMakeFiles" (
        rmdir /s /q "%BUILD_DIR%\CMakeFiles" >nul 2>nul
    )

    set "CMAKE_ARGS=-B "%BUILD_DIR%" -S "%ROOT_DIR%" -G "Visual Studio 17 2022" -A x64"
    if defined VCPKG_TOOLCHAIN (
        set "CMAKE_ARGS=!CMAKE_ARGS! -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" -DVCPKG_MANIFEST_MODE=OFF"
    )
    if defined QT6_DIR (
        set "CMAKE_ARGS=!CMAKE_ARGS! -DQt6_DIR="%QT6_DIR%""
    )
    if "%OLP_HEADLESS%"=="1" (
        set "CMAKE_ARGS=!CMAKE_ARGS! -DOLP_HEADLESS=ON"
    ) else (
        set "CMAKE_ARGS=!CMAKE_ARGS! -DOLP_HEADLESS=OFF"
    )

    cmake !CMAKE_ARGS!
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] CMake configuration failed.
        exit /b 1
    )
) else (
    echo [SiFastSuite Build] Valid CMake configuration detected.
)

REM Build targets using specified MSVC toolset version
echo [SiFastSuite Build] Building project targets (%CONFIG%)...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel -- /p:VCToolsVersion=14.44.35207
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

REM Run automated test suite
if "%DO_TESTS%"=="1" (
    echo ==============================================================================
    echo [SiFastSuite Build] Running verification tests...
    echo ==============================================================================
    ctest --test-dir "%BUILD_DIR%" -C %CONFIG% --output-on-failure
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] Verification tests failed.
        exit /b 1
    )
)

REM Run main CLI executable verification
if not exist "%BUILD_DIR%\%CONFIG%\SiFastSuiteOLP.exe" (
    echo [ERROR] SiFastSuiteOLP.exe was not produced.
    exit /b 1
)

echo ==============================================================================
echo [SiFastSuite Build] Executing SiFastSuiteOLP verification run...
echo ==============================================================================
"%BUILD_DIR%\%CONFIG%\SiFastSuiteOLP.exe" --verify
set APP_EXIT=!ERRORLEVEL!
if !APP_EXIT! neq 0 (
    echo [ERROR] SiFastSuiteOLP exited with code !APP_EXIT!.
    exit /b !APP_EXIT!
)

echo ==============================================================================
echo [SiFastSuite Build] Build and verification finished successfully.
echo ==============================================================================
exit /b 0
