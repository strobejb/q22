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
