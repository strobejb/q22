cmake_minimum_required(VERSION 3.16)

# ── Capstone disassembly library ──────────────────────────────────────────────
include(FetchContent)
set(CAPSTONE_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL   OFF CACHE BOOL "" FORCE)
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "" FORCE)
set(CAPSTONE_X86_SUPPORT    ON  CACHE BOOL "" FORCE)
set(CAPSTONE_ARM_SUPPORT    ON  CACHE BOOL "" FORCE)
set(CAPSTONE_AARCH64_SUPPORT ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    capstone
    GIT_REPOSITORY https://github.com/capstone-engine/capstone.git
    GIT_TAG        5.0.3
    GIT_SHALLOW    TRUE
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

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS DBus)

enable_testing()

add_subdirectory(src/HexView)
add_subdirectory(src/TypeLib)
add_subdirectory(src/HexEdit)
add_subdirectory(tests)
