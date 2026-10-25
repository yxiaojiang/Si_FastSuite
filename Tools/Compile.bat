@echo off
setlocal enabledelayedexpansion

REM ==============================================================================
REM SiFastSuite OLP Build Script
REM Configures and builds the project using CMake and Visual Studio (x64).
REM ==============================================================================

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build"
set "CONFIG=Release"

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

REM Create build directory if it does not exist
if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
)

REM Configure project with CMake if CMakeCache.txt does not exist
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [SiFastSuite Build] Generating build files with CMake...
    if "%OLP_HEADLESS%"=="1" (
        cmake -B "%BUILD_DIR%" -S "%ROOT_DIR%" -G "Visual Studio 17 2022" -A x64 -DOLP_HEADLESS=ON
    ) else (
        cmake -B "%BUILD_DIR%" -S "%ROOT_DIR%" -G "Visual Studio 17 2022" -A x64 -DOLP_HEADLESS=OFF
    )
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] CMake configuration failed.
        exit /b 1
    )
) else (
    echo [SiFastSuite Build] Existing CMake configuration detected.
)

REM Build all targets
echo [SiFastSuite Build] Building project targets (%CONFIG%)...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

REM Run automated test suite
echo ==============================================================================
echo [SiFastSuite Build] Running verification tests...
echo ==============================================================================
ctest --test-dir "%BUILD_DIR%" -C %CONFIG% --output-on-failure
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Verification tests failed.
    exit /b 1
)

REM Run main CLI executable verification
if not exist "%BUILD_DIR%\%CONFIG%\SiFastSuiteOLP.exe" (
    echo [ERROR] SiFastSuiteOLP.exe was not produced.
    exit /b 1
)
echo ==============================================================================
echo [SiFastSuite Build] Executing SiFastSuiteOLP verification run...
echo ==============================================================================
"%BUILD_DIR%\%CONFIG%\SiFastSuiteOLP.exe"
set APP_EXIT=!ERRORLEVEL!
if !APP_EXIT! neq 0 (
    echo [ERROR] SiFastSuiteOLP exited with code !APP_EXIT!.
    exit /b !APP_EXIT!
)

echo ==============================================================================
echo [SiFastSuite Build] Build and verification finished.
echo ==============================================================================
exit /b 0
