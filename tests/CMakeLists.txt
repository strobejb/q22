find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

add_executable(sequence_tests
    sequence_tests.cpp
)

target_link_libraries(sequence_tests PRIVATE
    HexView
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME sequence_tests COMMAND sequence_tests)
