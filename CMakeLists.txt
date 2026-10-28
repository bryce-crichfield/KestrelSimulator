cmake_minimum_required(VERSION 3.20)
project(KestrelSimulator LANGUAGES C CXX)

# The gate-level simulator and circuit editor, on its own. It loads components (yaml netlists)
# from Components/ beside this file.
#
# Configure and build:   cmake -S . -B Build && cmake --build Build   (or .\kestrel.ps1 --Build)
# Headless run:          Binary/KestrelSimulator --run COMPONENT [--program FILE.bin]
# Self test:             Binary/KestrelSimulator --self-test

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Executables land in Binary/ for every build type (no Debug/Release subfolders).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Binary)
foreach(config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_SOURCE_DIR}/Binary)
endforeach()

# ---------------------------------------------------------------------------
# Vendor libraries, fetched into Build/_deps on configure and pinned to exact commits.
include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG c1ab645ca298a2801097931d1079b10ff7eb9df8  # 5.5
    GIT_SHALLOW FALSE
)
# ImGui, rlImGui and imnodes have no CMake build we want; SOURCE_SUBDIR points at nothing so
# they are only downloaded, and we compile them ourselves below.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 5d4126876bc10396d4c6511853ff10964414c776  # v1.92.1
    SOURCE_SUBDIR none
)
FetchContent_Declare(rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG 118221c8a532501a3c2c236b62950877cbefc2b4  # Raylib_5_5
    SOURCE_SUBDIR none
)
FetchContent_Declare(imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG eb36902c892548ef94f88f51ad7e7c9c7058a71c  # master, works with ImGui 1.92
    SOURCE_SUBDIR none
)
# yaml-cpp reads and writes the component files.
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 56e3bb550c91fd7005566f19c079cb7a503223cf  # yaml-cpp-0.9.0
)
FetchContent_MakeAvailable(raylib imgui rlimgui imnodes yaml-cpp)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
# Big components overflow 16-bit draw-list indices.
target_compile_definitions(imgui PUBLIC "ImDrawIdx=unsigned int")

add_library(rlimgui STATIC ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
target_include_directories(rlimgui PUBLIC ${rlimgui_SOURCE_DIR})
target_link_libraries(rlimgui PUBLIC raylib imgui)

add_library(imnodes STATIC ${imnodes_SOURCE_DIR}/imnodes.cpp)
target_include_directories(imnodes PUBLIC ${imnodes_SOURCE_DIR})
target_link_libraries(imnodes PUBLIC imgui)

# ---------------------------------------------------------------------------
# KestrelSimulatorCore: components, their files and the gate-level simulation, without the editor.
set(COMPONENTS_DIR ${CMAKE_SOURCE_DIR}/Components)
set(SOURCE_DIR ${CMAKE_SOURCE_DIR}/Source)
add_library(KestrelSimulatorCore STATIC
    ${SOURCE_DIR}/Circuit.cpp
    ${SOURCE_DIR}/ComponentFile.cpp
    ${SOURCE_DIR}/KicadImport.cpp
    ${SOURCE_DIR}/Simulation.cpp
)
target_include_directories(KestrelSimulatorCore PUBLIC ${SOURCE_DIR})
target_link_libraries(KestrelSimulatorCore PUBLIC imgui yaml-cpp::yaml-cpp)
target_compile_definitions(KestrelSimulatorCore PRIVATE COMPONENTS_PATH="${COMPONENTS_DIR}/")
if(MSVC)
    target_compile_options(KestrelSimulatorCore PUBLIC /W4)
else()
    target_compile_options(KestrelSimulatorCore PUBLIC -Wall -Wextra)
endif()

# Circuit editor and gate-level simulator -> Binary/KestrelSimulator
add_executable(KestrelSimulator
    ${SOURCE_DIR}/Editor.cpp
    ${SOURCE_DIR}/EditorCanvas.cpp
    ${SOURCE_DIR}/EditorImport.cpp
    ${SOURCE_DIR}/FileDialog.cpp
    ${SOURCE_DIR}/EditorMemory.cpp
    ${SOURCE_DIR}/EditorPanels.cpp
    ${SOURCE_DIR}/HeadlessRun.cpp
    ${SOURCE_DIR}/Main.cpp
    ${SOURCE_DIR}/SelfTest.cpp
)
target_link_libraries(KestrelSimulator PRIVATE KestrelSimulatorCore raylib rlimgui imnodes)
if(WIN32)
    target_link_libraries(KestrelSimulator PRIVATE comdlg32)  # the Open file dialog
endif()

enable_testing()
add_test(NAME simulator_self_test COMMAND KestrelSimulator --self-test)
