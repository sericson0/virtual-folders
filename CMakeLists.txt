cmake_minimum_required(VERSION 3.22)

# Control /MT vs /MD via MSVC_RUNTIME_LIBRARY target property (CMake 3.15+).
cmake_policy(SET CMP0091 NEW)

project(TigerFolders VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statically link the MSVC C runtime so the plugin DLL has no external
# VCRUNTIME140/MSVCP140 dependency. VirtualDJ 2025 reports v8 plugins with
# dynamic CRT linkage as "incompatible".
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ══════════════════════════════════════════════════════════════════════════════
#  TigerFolders — VirtualDJ native plugin (Windows-only MODULE DLL, no JUCE)
#  Builds a tree of VDJ virtual folders from the tags of a selected folder tree.
# ══════════════════════════════════════════════════════════════════════════════

if(NOT WIN32)
    message(FATAL_ERROR "TigerFolders is Windows-only.")
endif()

add_library(TigerFolders MODULE
    Source/TigerFolders.cpp
    Source/TigerFoldersUI.cpp
    Source/TigerFoldersPath.cpp
    Source/TigerFoldersScan.cpp
    Source/TigerFoldersWrite.cpp
    Source/TigerFoldersHelpers.cpp
    Source/TigerFolders.def
)

set_target_properties(TigerFolders PROPERTIES
    OUTPUT_NAME "TigerFolders"
    SUFFIX      ".dll"
)

target_compile_definitions(TigerFolders PRIVATE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

target_compile_features(TigerFolders PRIVATE cxx_std_17)

# /utf-8 : treat source (and execution charset) as UTF-8 so wide-string
#          literals with em-dashes / arrows / symbols decode correctly.
# /FIshlobj.h : force-include before all sources so shell path APIs resolve
#          under WIN32_LEAN_AND_MEAN.
target_compile_options(TigerFolders PRIVATE /utf-8 /FIshlobj.h)

target_include_directories(TigerFolders PRIVATE "${CMAKE_SOURCE_DIR}/Source")

target_link_libraries(TigerFolders PRIVATE
    gdi32
    comctl32
    shell32
    ole32
    uxtheme
)

# Best-effort copy of the built DLL into the VDJ plugin folder. Wrapped so the
# build never fails when VirtualDJ is absent OR currently has the DLL locked
# (i.e. the plugin is open) — `& exit /b 0` swallows the error either way.
set(VDJ_PLUGIN_DIR "$ENV{LOCALAPPDATA}/VirtualDJ/Plugins64/SoundEffect/TigerFolders")
add_custom_command(TARGET TigerFolders POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DSRC=$<TARGET_FILE:TigerFolders>
            -DDEST_DIR=${VDJ_PLUGIN_DIR}
            -P "${CMAKE_SOURCE_DIR}/cmake/install_dll.cmake"
    COMMENT "Copying TigerFolders.dll to VirtualDJ plugin folder (skipped if VDJ absent/locked)"
)
