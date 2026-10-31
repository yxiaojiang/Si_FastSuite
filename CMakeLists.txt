cmake_minimum_required(VERSION 3.20)
project(SiFastSuiteOLP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive- /EHsc)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ------------------------------------------------------------------------------
# Options & Dependency Auto-Detection (Qt6, OpenCASCADE, Eigen3, FCL)
# ------------------------------------------------------------------------------
option(OLP_HEADLESS    "Algorithm-only build. Does not require Qt6 or OpenCASCADE." OFF)
option(OLP_BUILD_TESTS  "Build unit & Phase-0 verification tests" ON)

if(OLP_HEADLESS)
    message(STATUS "[SiFastSuiteOLP] OLP_HEADLESS=ON - Qt6 and OpenCASCADE are not used")
else()
    find_package(Qt6 COMPONENTS Core Gui Widgets PrintSupport REQUIRED)
    find_package(OpenCASCADE REQUIRED)
    message(STATUS "[SiFastSuiteOLP] Qt6 ${Qt6_VERSION}, OpenCASCADE ${OpenCASCADE_VERSION}")
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
endif()

option(OLP_ENABLE_FCL "Enable FCL collision acceleration when the package is installed" ON)

find_package(Eigen3 QUIET)
if(Eigen3_FOUND)
    message(STATUS "[SiFastSuiteOLP] Found Eigen3")
endif()

find_package(nlohmann_json CONFIG QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "[SiFastSuiteOLP] Found nlohmann_json")
endif()

if(OLP_ENABLE_FCL)
    find_package(fcl QUIET)
    if(fcl_FOUND)
        message(STATUS "[SiFastSuiteOLP] Found FCL")
    else()
        message(STATUS "[SiFastSuiteOLP] FCL was not found; mesh distance is not linked")
    endif()
endif()

# Pass workspace root path for SampleScenes discovery in tests & spikes
add_compile_definitions(OLP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# ------------------------------------------------------------------------------
# Layer 4: Data & Geometry Layer (olp_core, olp_cad, olp_device)
# ------------------------------------------------------------------------------
add_library(olp_core STATIC
    src/core/ProjectModel.cpp
)
target_include_directories(olp_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(Eigen3_FOUND)
    target_link_libraries(olp_core PUBLIC Eigen3::Eigen)
    target_compile_definitions(olp_core PUBLIC OLP_HAS_EIGEN3=1)
endif()
if(nlohmann_json_FOUND)
    target_link_libraries(olp_core PUBLIC nlohmann_json::nlohmann_json)
    target_compile_definitions(olp_core PUBLIC OLP_HAS_NLOHMANN_JSON=1)
endif()

add_library(olp_cad STATIC
    src/cad/OcctCadKernel.cpp
    src/cad/OcctCadKernelQuery.cpp
    src/cad/OcctCadKernelMesh.cpp
)
target_include_directories(olp_cad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(olp_cad PUBLIC olp_core)
if(OpenCASCADE_FOUND)
    target_include_directories(olp_cad PUBLIC ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(olp_cad PUBLIC ${OpenCASCADE_LIBRARIES})
    target_compile_definitions(olp_cad PUBLIC OLP_HAS_OCCT=1)
endif()

add_library(olp_device STATIC
    src/device/RobotModel.cpp
)
target_include_directories(olp_device PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(olp_device PUBLIC olp_core)

# ------------------------------------------------------------------------------
# Layer 3: Algorithm Layer (olp_algo - Pure C++, UI-independent, 100% testable)
# ------------------------------------------------------------------------------
add_library(olp_algo STATIC
    src/algo/OpwKinematics.cpp
    src/algo/SeamAndToolpathAlgo.cpp
    src/algo/FclCollisionQueries.cpp
    src/algo/CollisionAndSim.cpp
    src/algo/simulation/TrajectorySimulator.cpp
)
target_include_directories(olp_algo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(olp_algo PUBLIC olp_core olp_cad olp_device)
if(fcl_FOUND)
    target_link_libraries(olp_algo PUBLIC fcl)
    target_compile_definitions(olp_algo PUBLIC OLP_HAS_FCL=1)
endif()

# ------------------------------------------------------------------------------
# Layer 5: I/O & Post-Processing Layer (olp_io)
# ------------------------------------------------------------------------------
add_library(olp_io STATIC
    src/io/FanucLsPostProcessor.cpp
    src/io/HtmlAnimationDataPrep.cpp
    src/io/HtmlAnimationExporter.cpp
    src/io/ProjectSerializer.cpp
    src/io/Utf8Path.cpp
)
target_include_directories(olp_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(olp_io PUBLIC olp_core olp_device olp_algo)

# ------------------------------------------------------------------------------
# Layer 2: Application Business Layer (olp_app - M01 Command/Document & M04)
# ------------------------------------------------------------------------------
add_library(olp_app STATIC
    src/app/CommandManager.cpp
    src/app/WorkcellLayoutService.cpp
)
target_include_directories(olp_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(olp_app PUBLIC olp_core olp_cad olp_device olp_algo olp_io)

# ------------------------------------------------------------------------------
# Layer 1: Presentation & Viewport Layer (olp_ui - Qt6 + OCCT AIS + TcpGizmo)
# ------------------------------------------------------------------------------
add_library(olp_ui STATIC
    src/ui/TcpGizmo.cpp
    src/ui/OcctViewport.cpp
    src/ui/OcctViewportPicking.cpp
    src/ui/OcctViewportGizmo.cpp
    src/ui/OcctViewportPlayback.cpp
    src/ui/MainWindow.cpp
    src/ui/SampleSession.cpp
)
if(Qt6_FOUND)
    target_sources(olp_ui PRIVATE
        src/ui/ShellChromeHelpers.cpp
        src/ui/GizmoDragPreviewHandler.cpp
        src/ui/ShellWindow.cpp
        src/ui/TwistScanWidget.cpp
        src/ui/IssueListPanel.cpp
        src/ui/TargetWaypointTable.cpp
        src/ui/TargetPropertyPanel.cpp
        src/ui/TimelinePanel.cpp
        src/ui/LsExportDialog.cpp
        src/ui/WorkcellPdfReportExporter.cpp
        include/olp/ui/GizmoDragPreviewHandler.h
        include/olp/ui/ShellWindow.h
        include/olp/ui/TwistScanWidget.h
        include/olp/ui/OcctViewport.h
        include/olp/ui/LsExportDialog.h
        include/olp/ui/WorkcellPdfReportExporter.h
    )
endif()
target_include_directories(olp_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(olp_ui PUBLIC olp_app)
if(Qt6_FOUND)
    target_link_libraries(olp_ui PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport)
    target_compile_definitions(olp_ui PUBLIC OLP_HAS_QT6=1)
endif()
if(OpenCASCADE_FOUND)
    target_include_directories(olp_ui PUBLIC ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(olp_ui PUBLIC ${OpenCASCADE_LIBRARIES})
    target_compile_definitions(olp_ui PUBLIC OLP_HAS_OCCT=1)
endif()

# ------------------------------------------------------------------------------
# Main Application Executable (SiFastSuiteOLP)
# ------------------------------------------------------------------------------
add_executable(SiFastSuiteOLP
    src/main.cpp
)
target_link_libraries(SiFastSuiteOLP PRIVATE olp_ui)

# ------------------------------------------------------------------------------
# Automated Verification & Unit Tests (olp_tests)
# ------------------------------------------------------------------------------
if(OLP_BUILD_TESTS)
    enable_testing()
    add_executable(olp_tests
        tests/test_main.cpp
        tests/test_kinematics.cpp
        tests/test_cad.cpp
        tests/test_toolpath.cpp
        tests/test_process.cpp
        tests/test_collision.cpp
        tests/test_fcl_spike.cpp
        tests/test_ui.cpp
        tests/test_simulation.cpp
        tests/test_postprocessor.cpp
        tests/test_report.cpp
        tests/test_project_io.cpp
        tests/test_tjoint_regression.cpp
    )
    target_link_libraries(olp_tests PRIVATE olp_ui)
    if(Qt6_FOUND)
        get_filename_component(OLP_QT_ROOT "${Qt6_DIR}/../../.." ABSOLUTE)
        target_compile_definitions(olp_tests PRIVATE "OLP_QT_PLUGINS=\"${OLP_QT_ROOT}/plugins\"")
    endif()
    add_test(NAME SiFastSuiteOLP_AllTests COMMAND olp_tests)
endif()
