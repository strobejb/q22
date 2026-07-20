cmake_minimum_required(VERSION 3.16)

# ── Capstone disassembly library ──────────────────────────────────────────────
include(FetchContent)
set(CAPSTONE_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL   OFF CACHE BOOL "" FORCE)
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "" FORCE)
set(CAPSTONE_X86_SUPPORT    ON  CACHE BOOL "" FORCE)
set(CAPSTONE_ARM_SUPPORT    ON  CACHE BOOL "" FORCE)
set(CAPSTONE_AARCH64_SUPPORT ON CACHE BOOL "" FORCE)
set(CAPSTONE_WASM_SUPPORT   ON  CACHE BOOL "" FORCE)
FetchContent_Declare(
    capstone
    GIT_REPOSITORY https://github.com/capstone-engine/capstone.git
    GIT_TAG        5.0.3
    GIT_SHALLOW    TRUE
    SOURCE_DIR     ${CMAKE_SOURCE_DIR}/src/capstone
)
FetchContent_MakeAvailable(capstone)
if(TARGET capstone)
    target_compile_options(capstone PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-w>
        $<$<C_COMPILER_ID:MSVC>:/W0>
    )
endif()

set(HEXEDIT_VERSION_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/HexEdit/version.h")
file(STRINGS "${HEXEDIT_VERSION_HEADER}" HEXEDIT_VERSION_LINES
    REGEX "^#define VERSION_(MAJOR|MINOR|PATCH|BUILD_COUNT)[ \t]+[0-9]+")
foreach(_version_line IN LISTS HEXEDIT_VERSION_LINES)
    if(_version_line MATCHES "^#define VERSION_([A-Z_]+)[ \t]+([0-9]+)")
        set(HEXEDIT_VERSION_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endif()
endforeach()
foreach(_version_part MAJOR MINOR PATCH BUILD_COUNT)
    if(NOT DEFINED HEXEDIT_VERSION_${_version_part})
        message(FATAL_ERROR "Missing VERSION_${_version_part} in ${HEXEDIT_VERSION_HEADER}")
    endif()
endforeach()

project(HexEdit
    VERSION ${HEXEDIT_VERSION_MAJOR}.${HEXEDIT_VERSION_MINOR}.${HEXEDIT_VERSION_PATCH}
    LANGUAGES CXX C)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Single-config generators otherwise leave this empty.  Qt Creator's default
# desktop profile is Debug, and it requires a matching named configuration.
# Explicit command-line and CI selections (for example Release) take priority.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

# CMake caches each Qt component's package location independently. Reusing a
# build directory after switching Qt kits can otherwise combine components from
# different installations and produce a binary with an incoherent Qt runtime.
# Each kit must have its own build directory.
set(_q22_requested_qt_dir "")
if(DEFINED Qt6_DIR AND NOT Qt6_DIR STREQUAL "")
    set(_q22_requested_qt_dir "${Qt6_DIR}")
elseif(DEFINED Qt5_DIR AND NOT Qt5_DIR STREQUAL "")
    set(_q22_requested_qt_dir "${Qt5_DIR}")
endif()
if(DEFINED Q22_CONFIGURED_QT_DIR AND NOT _q22_requested_qt_dir STREQUAL ""
   AND NOT Q22_CONFIGURED_QT_DIR STREQUAL _q22_requested_qt_dir)
    message(FATAL_ERROR
        "This build directory was configured with Qt at '${Q22_CONFIGURED_QT_DIR}', "
        "but CMake was asked to use '${_q22_requested_qt_dir}'.\n"
        "Create a new build directory for the selected Qt Creator kit, then configure again.")
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS DBus)
find_package(ZLIB QUIET)

set(Q22_CONFIGURED_QT_DIR "${Qt${QT_VERSION_MAJOR}_DIR}" CACHE INTERNAL
    "Qt package directory used for this build tree")

enable_testing()

add_subdirectory(src/HexView)
add_subdirectory(src/causeway)
add_subdirectory(src/HexEdit)
add_subdirectory(tests)
