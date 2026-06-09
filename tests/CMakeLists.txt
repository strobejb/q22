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
