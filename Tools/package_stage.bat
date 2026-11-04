@echo off
setlocal enabledelayedexpansion

REM ==============================================================================
REM SiFastSuite OLP Standalone Staging Package Script
REM Assembles a self-contained runtime sandbox in build\dist with all Qt, OCCT,
REM vcpkg, CRT dependencies and sample scenes, verifying offline execution.
REM ==============================================================================

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT_DIR%\build"
set "DIST_DIR=%BUILD_DIR%\dist"
set "DO_BUILD=1"
set "DO_VERIFY=1"

REM Parse arguments
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--skip-build" (
    set "DO_BUILD=0"
    shift
    goto parse_args
)
if /i "%~1"=="--no-verify" (
    set "DO_VERIFY=0"
    shift
    goto parse_args
)
shift
goto parse_args
:args_done

echo ==============================================================================
echo [SiFastSuite Stage] Starting Standalone Sandbox Assembly
echo [SiFastSuite Stage] Project Root : %ROOT_DIR%
echo [SiFastSuite Stage] Dist Folder  : %DIST_DIR%
echo ==============================================================================

REM Check dependency tools and folders
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
if not defined CRT_DIR (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT" (
        set "CRT_DIR=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT"
    ) else (
        for /d %%D in ("C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT") do (
            set "CRT_DIR=%%D"
        )
    )
)

if not exist "%QT_BIN%\windeployqt.exe" (
    echo [ERROR] windeployqt.exe not found at %QT_BIN%.
    exit /b 1
)
if not exist "%VCPKG_BIN%" (
    echo [ERROR] vcpkg bin directory not found at %VCPKG_BIN%.
    exit /b 1
)
if not exist "%CRT_DIR%" (
    echo [ERROR] VC143 CRT redist directory not found at %CRT_DIR%.
    exit /b 1
)

REM Build release binary if requested
if "%DO_BUILD%"=="1" (
    echo [SiFastSuite Stage] Building Release executable...
    call "%SCRIPT_DIR%Compile.bat" Release --no-tests
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] Release compilation failed.
        exit /b 1
    )
)

if not exist "%BUILD_DIR%\Release\SiFastSuiteOLP.exe" (
    echo [ERROR] SiFastSuiteOLP.exe not found in %BUILD_DIR%\Release.
    exit /b 1
)

REM Prepare clean dist directory
echo [SiFastSuite Stage] Preparing clean dist folder...
if exist "%DIST_DIR%" (
    rmdir /s /q "%DIST_DIR%" >nul 2>nul
)
mkdir "%DIST_DIR%"
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to create dist folder: %DIST_DIR%
    exit /b 1
)

REM 1. Copy main executable
echo [SiFastSuite Stage] Copying SiFastSuiteOLP.exe...
copy /y "%BUILD_DIR%\Release\SiFastSuiteOLP.exe" "%DIST_DIR%\SiFastSuiteOLP.exe" >nul
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to copy SiFastSuiteOLP.exe.
    exit /b 1
)

REM 2. Run windeployqt for Qt runtime deployment
echo [SiFastSuite Stage] Deploying Qt runtime via windeployqt...
set "WINDEPLOY_PATH=%QT_BIN%;%PATH%"
pushd "%DIST_DIR%"
"%QT_BIN%\windeployqt.exe" --release --no-compiler-runtime --no-translations --no-opengl-sw --no-printsupport --plugindir "%DIST_DIR%\plugins" "%DIST_DIR%\SiFastSuiteOLP.exe" >nul
set "DEPLOY_EXIT=!ERRORLEVEL!"
popd
if !DEPLOY_EXIT! neq 0 (
    echo [ERROR] windeployqt failed with code !DEPLOY_EXIT!.
    exit /b 1
)

REM Remove Qt6PrintSupport.dll if pulled by accident
if exist "%DIST_DIR%\Qt6PrintSupport.dll" (
    del /f /q "%DIST_DIR%\Qt6PrintSupport.dll" >nul 2>nul
)

REM 3. Create qt.conf to isolate Qt plugin discovery
echo [SiFastSuite Stage] Creating qt.conf...
(
    echo [Paths]
    echo Plugins = plugins
) > "%DIST_DIR%\qt.conf"

REM 4. Copy 31 vcpkg DLLs
echo [SiFastSuite Stage] Copying 31 vcpkg DLLs...
set "VCPKG_DLL_LIST=brotlicommon.dll brotlidec.dll bz2.dll ccd.dll freetype.dll libpng16.dll z.dll TKBO.dll TKBRep.dll TKCAF.dll TKCDF.dll TKDE.dll TKDESTEP.dll TKG2d.dll TKG3d.dll TKGeomAlgo.dll TKGeomBase.dll TKHLR.dll TKLCAF.dll TKMath.dll TKMesh.dll TKOpenGl.dll TKPrim.dll TKService.dll TKShHealing.dll TKTopAlgo.dll TKV3d.dll TKVCAF.dll TKXCAF.dll TKXSBase.dll TKernel.dll"

set "MISSING_DLL_COUNT=0"
for %%F in (%VCPKG_DLL_LIST%) do (
    if exist "%VCPKG_BIN%\%%F" (
        copy /y "%VCPKG_BIN%\%%F" "%DIST_DIR%\%%F" >nul
    ) else (
        echo [ERROR] Required vcpkg DLL missing: %%F
        set /a MISSING_DLL_COUNT+=1
    )
)
if !MISSING_DLL_COUNT! gtr 0 (
    echo [ERROR] Total !MISSING_DLL_COUNT! vcpkg DLLs missing.
    exit /b 1
)

REM 5. Copy MSVC VC143 CRT DLLs
echo [SiFastSuite Stage] Copying MSVC CRT runtime from %CRT_DIR%...
copy /y "%CRT_DIR%\*.dll" "%DIST_DIR%\" >nul
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to copy VC143 CRT DLLs.
    exit /b 1
)

if not exist "%DIST_DIR%\msvcp140.dll" (
    echo [ERROR] msvcp140.dll missing from dist folder.
    exit /b 1
)
if not exist "%DIST_DIR%\vcruntime140.dll" (
    echo [ERROR] vcruntime140.dll missing from dist folder.
    exit /b 1
)

REM 6. Copy SampleScenes and resources
echo [SiFastSuite Stage] Copying SampleScenes and resources...
robocopy "%ROOT_DIR%\SampleScenes" "%DIST_DIR%\SampleScenes" /E /NFL /NDL /NJH /NJS /nc /ns /np >nul
if !ERRORLEVEL! geq 8 (
    echo [ERROR] Robocopy failed for SampleScenes.
    exit /b 1
)

robocopy "%ROOT_DIR%\resources" "%DIST_DIR%\resources" /E /NFL /NDL /NJH /NJS /nc /ns /np >nul
if !ERRORLEVEL! geq 8 (
    echo [ERROR] Robocopy failed for resources.
    exit /b 1
)

echo [SiFastSuite Stage] Sandbox staging successfully assembled at %DIST_DIR%.

REM 7. Smoke verification with stripped environment PATH
if "%DO_VERIFY%"=="1" (
    echo ==============================================================================
    echo [SiFastSuite Stage] Running isolated smoke verification: --verify
    echo ==============================================================================
    
    set "OLD_PATH=%PATH%"
    REM Strip external Qt and vcpkg from PATH, keeping only system standard paths
    set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem"

    "%DIST_DIR%\SiFastSuiteOLP.exe" --verify
    set "VERIFY_EXIT=!ERRORLEVEL!"
    
    set "PATH=!OLD_PATH!"
    
    if !VERIFY_EXIT! neq 0 (
        echo [ERROR] Isolated verification failed with exit code !VERIFY_EXIT!.
        exit /b !VERIFY_EXIT!
    )

    REM Verify that no output files were written directly inside dist folder
    if exist "%DIST_DIR%\WELD_001.ls" (
        echo [ERROR] WELD_001.ls was unexpectedly created inside dist folder.
        exit /b 1
    )
    if exist "%DIST_DIR%\WELD_001_Preview.html" (
        echo [ERROR] WELD_001_Preview.html was unexpectedly created inside dist folder.
        exit /b 1
    )
    
    echo [SiFastSuite Stage] Verification passed. Outputs cleanly decoupled to temp directory.
)

echo ==============================================================================
echo [SiFastSuite Stage] Standalone sandbox packaging stage completed successfully.
echo ==============================================================================
exit /b 0
