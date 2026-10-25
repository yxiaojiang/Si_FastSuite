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
option(OLP_ENABLE_QT6   "Enable Qt6 GUI Workbench & Dock Panels if available" ON)
option(OLP_ENABLE_OCCT  "Enable OpenCASCADE B-Rep & AIS 3D Viewport if available" ON)
option(OLP_ENABLE_FCL   "Enable FCL/Coal BVH collision acceleration if available" ON)
option(OLP_BUILD_TESTS  "Build unit & Phase-0 verification tests" ON)

if(OLP_ENABLE_QT6)
    find_package(Qt6 COMPONENTS Core Gui Widgets PrintSupport QUIET)
    if(Qt6_FOUND)
        message(STATUS "[SiFastSuiteOLP] Found Qt6 (${Qt6_VERSION}) - enabling native Qt6 UI")
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)
        set(CMAKE_AUTOUIC ON)
    else()
        message(STATUS "[SiFastSuiteOLP] Qt6 not found in current environment - building headless/stub UI layer")
    endif()
endif()

if(OLP_ENABLE_OCCT)
    find_package(OpenCASCADE QUIET)
    if(OpenCASCADE_FOUND)
        message(STATUS "[SiFastSuiteOLP] Found OpenCASCADE (${OpenCASCADE_VERSION}) - enabling OCCT B-Rep & AIS")
    else()
        message(STATUS "[SiFastSuiteOLP] OpenCASCADE not found in current environment - using built-in STEP/STL B-Rep kernel adapter")
    endif()
endif()

find_package(Eigen3 QUIET)
if(Eigen3_FOUND)
    message(STATUS "[SiFastSuiteOLP] Found Eigen3")
endif()

if(OLP_ENABLE_FCL)
    find_package(fcl QUIET)
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

add_library(olp_cad STATIC
    src/cad/OcctCadKernel.cpp
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
    src/algo/CollisionAndSim.cpp
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
    src/io/HtmlAnimationExporter.cpp
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
    src/ui/MainWindow.cpp
)
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
    )
    target_link_libraries(olp_tests PRIVATE olp_ui)
    add_test(NAME SiFastSuiteOLP_AllTests COMMAND olp_tests)
endif()
