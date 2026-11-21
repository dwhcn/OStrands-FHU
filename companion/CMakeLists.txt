cmake_minimum_required(VERSION 3.25)
project(FHUCompanion VERSION 0.8.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(FHU_ENGLISH_MENU "Build the English menu" OFF)
set(FHU_COMMONLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG" CACHE PATH "CommonLibSSE-NG checkout")
if(NOT EXISTS "${FHU_COMMONLIB_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "CommonLibSSE-NG is required. See BUILD.txt.")
endif()

set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${FHU_COMMONLIB_DIR}" CommonLibSSE EXCLUDE_FROM_ALL)
include("${FHU_COMMONLIB_DIR}/cmake/CommonLibSSE.cmake")

set(menu_source src/SettingsMenu.cpp)
if(FHU_ENGLISH_MENU)
    set(menu_source localization/en/SettingsMenu.cpp)
endif()

add_commonlibsse_plugin(
    FHUCompanion
    NAME "FHUCompanion"
    AUTHOR "Saint"
    SOURCES
        src/main.cpp
        src/Anatomy.cpp
        src/Arousal.cpp
        src/AutoPairs.cpp
        src/Body.cpp
        src/FluidManager.cpp
        src/Hooks.cpp
        src/OCumBridge.cpp
        src/OStimBridge.cpp
        src/Revealing.cpp
        src/SexLabBridge.cpp
        src/Settings.cpp
        ${menu_source}
        src/StrandManager.cpp
        src/fluid/Clusters.cpp
        src/fluid/Ground.cpp
        src/fluid/Skin.cpp
        src/fluid/Solver.cpp
        src/fluid/Surface.cpp
)

find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)
target_compile_definitions(FHUCompanion PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(FHUCompanion PRIVATE src extern/ostim extern/SKSEMenuFramework ${SIMPLEINI_INCLUDE_DIRS})
target_precompile_headers(FHUCompanion PRIVATE src/PCH.h)
target_compile_options(FHUCompanion PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
