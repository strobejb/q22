find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

add_executable(sequence_tests
    sequence_tests.cpp
)

target_link_libraries(sequence_tests PRIVATE
    HexView
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME sequence_tests COMMAND sequence_tests)
get_filename_component(QT_CMAKE_DIR "${Qt${QT_VERSION_MAJOR}_DIR}" DIRECTORY)
get_filename_component(QT_LIB_DIR "${QT_CMAKE_DIR}" DIRECTORY)
get_filename_component(QT_PREFIX_DIR "${QT_LIB_DIR}" DIRECTORY)
set(QT_RUNTIME_BIN_DIR "${QT_PREFIX_DIR}/bin")
set_tests_properties(sequence_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${QT_RUNTIME_BIN_DIR}"
)

add_executable(bookmark_tests
    bookmark_tests.cpp
)

target_link_libraries(bookmark_tests PRIVATE
    HexView
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME bookmark_tests COMMAND bookmark_tests)
set_tests_properties(bookmark_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${QT_RUNTIME_BIN_DIR};QT_QPA_PLATFORM=set:offscreen"
)

add_executable(findpanel_tests
    findpanel_tests.cpp
    ../src/HexEdit/panels/findpattern.cpp
)

target_include_directories(findpanel_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/HexEdit
)
target_link_libraries(findpanel_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME findpanel_tests COMMAND findpanel_tests)
set_tests_properties(findpanel_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${QT_RUNTIME_BIN_DIR}"
)

add_executable(disasm_tests
    disasm_tests.cpp
)

target_link_libraries(disasm_tests PRIVATE
    capstone
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME disasm_tests COMMAND disasm_tests)
set_tests_properties(disasm_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${QT_RUNTIME_BIN_DIR}"
)

add_executable(causeway_tests
    causeway_tests.cpp
)

target_link_libraries(causeway_tests PRIVATE
    causeway
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)
target_compile_definitions(causeway_tests PRIVATE
    CAUSEWAY_TEST_DATA_DIR="${CAUSEWAY_DATA_BINARY_DIR}"
)
if(TARGET causewayData)
    add_dependencies(causeway_tests causewayData)
endif()

add_test(NAME causeway_tests COMMAND causeway_tests)
set_tests_properties(causeway_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${QT_RUNTIME_BIN_DIR}"
)

set(STRUCTVIEW_TEST_SOURCES
    structview_tests.cpp
    structview/structview_testsupport.h
    structview/formats/archive_tests.cpp
    structview/formats/dex_wasm_tests.cpp
    structview/formats/disk_image_tests.cpp
    structview/formats/dtb_tests.cpp
    structview/formats/elf_tests.cpp
    structview/formats/font_tests.cpp
    structview/formats/image_tests.cpp
    structview/formats/java_class_tests.cpp
    structview/formats/macho_tests.cpp
    structview/formats/media_tests.cpp
    structview/formats/pe_tests.cpp
    structview/formats/root_format_tests.cpp
    structview/manager/definition_manager_tests.cpp
    structview/model/tree_model_tests.cpp
    structview/renderer/dynamic_placement_tests.cpp
    structview/renderer/raw_renderer_tests.cpp
    structview/renderer/semantic_renderer_tests.cpp
    ../src/HexEdit/settings/appconfig.cpp
    ../src/HexEdit/structview/dexsemanticview.cpp
    ../src/HexEdit/structview/elfsemanticview.cpp
    ../src/HexEdit/structview/pesemanticview.cpp
    ../src/HexEdit/structview/structuredefinitionmanager.cpp
    ../src/HexEdit/structview/structurerenderengine.cpp
    ../src/HexEdit/structview/structuresemanticview.cpp
    ../src/HexEdit/structview/structuretypenameformatter.cpp
    ../src/HexEdit/structview/structuretreemodel.cpp
    ../src/HexEdit/structview/structurevaluebuilder.cpp
)

add_executable(structview_tests
    ${STRUCTVIEW_TEST_SOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "structview_tests" FILES
    structview_tests.cpp
    structview/structview_testsupport.h
    structview/formats/archive_tests.cpp
    structview/formats/dex_wasm_tests.cpp
    structview/formats/disk_image_tests.cpp
    structview/formats/dtb_tests.cpp
    structview/formats/elf_tests.cpp
    structview/formats/font_tests.cpp
    structview/formats/image_tests.cpp
    structview/formats/java_class_tests.cpp
    structview/formats/macho_tests.cpp
    structview/formats/media_tests.cpp
    structview/formats/pe_tests.cpp
    structview/formats/root_format_tests.cpp
    structview/manager/definition_manager_tests.cpp
    structview/model/tree_model_tests.cpp
    structview/renderer/dynamic_placement_tests.cpp
    structview/renderer/raw_renderer_tests.cpp
    structview/renderer/semantic_renderer_tests.cpp
)

target_include_directories(structview_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/HexEdit
    ${PROJECT_SOURCE_DIR}/tests
)

target_link_libraries(structview_tests PRIVATE
    causeway
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)
target_compile_definitions(structview_tests PRIVATE
    CAUSEWAY_TEST_DATA_DIR="${CAUSEWAY_DATA_BINARY_DIR}"
)
if(TARGET causewayData)
    add_dependencies(structview_tests causewayData)
endif()

set(STRUCTVIEW_TEST_SUITES
    StructViewArchiveTests
    StructViewDexWasmTests
    StructViewDiskImageTests
    StructViewDtbTests
    StructViewElfTests
    StructViewFontTests
    StructViewImageTests
    StructViewJavaClassTests
    StructViewMachOTests
    StructViewMediaTests
    StructViewPeTests
    StructViewRootFormatTests
    StructViewDefinitionManagerTests
    StructViewTreeModelTests
    StructViewDynamicPlacementTests
    StructViewRawRendererTests
    StructViewSemanticRendererTests
)

foreach(_structview_suite IN LISTS STRUCTVIEW_TEST_SUITES)
    add_test(NAME "structview_tests.${_structview_suite}"
        COMMAND structview_tests --suite "${_structview_suite}"
    )
    set_tests_properties("structview_tests.${_structview_suite}" PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${QT_RUNTIME_BIN_DIR}"
    )
endforeach()

add_executable(stringscan_tests
    stringscan_tests.cpp
    ../src/HexEdit/filestats/stringscan.cpp
)

target_include_directories(stringscan_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/HexEdit
)

target_link_libraries(stringscan_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME stringscan_tests COMMAND stringscan_tests)
set_tests_properties(stringscan_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${QT_RUNTIME_BIN_DIR}"
)

add_executable(exportformat_tests
    exportformat_tests.cpp
    ../src/HexEdit/dialogs/exportformat.cpp
)

target_include_directories(exportformat_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/HexEdit
)

target_link_libraries(exportformat_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME exportformat_tests COMMAND exportformat_tests)
set_tests_properties(exportformat_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${QT_RUNTIME_BIN_DIR}"
)

add_executable(importformat_tests
    importformat_tests.cpp
    ../src/HexEdit/dialogs/exportformat.cpp
    ../src/HexEdit/dialogs/importformat.cpp
)

target_include_directories(importformat_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/HexEdit
)

target_link_libraries(importformat_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME importformat_tests COMMAND importformat_tests)
set_tests_properties(importformat_tests PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${QT_RUNTIME_BIN_DIR}"
)
