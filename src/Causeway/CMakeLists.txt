add_library(Causeway STATIC
    display.cpp
    error.cpp
    lexer.cpp
    parser.cpp
    parse_expr.cpp
    parse_typedecl.cpp
    type_utils.cpp
    error.h
    errordef.h
    expr.h
    keywords.h
    lexer.h
    parser.h
    stmt.h
    stringprint.h
    tchar_compat.h
    types.h
)

target_include_directories(Causeway PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(CAUSEWAY_DATA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/strata")
set(CAUSEWAY_DATA_BINARY_DIR "${PROJECT_BINARY_DIR}/strata")
file(GLOB CAUSEWAY_STANDARD_FILES CONFIGURE_DEPENDS
    "${CAUSEWAY_DATA_SOURCE_DIR}/*.struct"
)

add_custom_target(CausewayData ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CAUSEWAY_DATA_BINARY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CAUSEWAY_STANDARD_FILES}
        "${CAUSEWAY_DATA_BINARY_DIR}"
    SOURCES ${CAUSEWAY_STANDARD_FILES}
    COMMENT "Copying Causeway/Strata runtime data"
)

include(GNUInstallDirs)
install(FILES ${CAUSEWAY_STANDARD_FILES}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/hexedit/strata"
)

set(CAUSEWAY_DATA_BINARY_DIR "${CAUSEWAY_DATA_BINARY_DIR}" PARENT_SCOPE)
