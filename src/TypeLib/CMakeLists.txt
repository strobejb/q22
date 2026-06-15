add_library(TypeLib STATIC
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

target_include_directories(TypeLib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(TYPELIB_DATA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/typelib")
set(TYPELIB_DATA_BINARY_DIR "${PROJECT_BINARY_DIR}/typelib")
file(GLOB TYPELIB_STANDARD_FILES CONFIGURE_DEPENDS
    "${TYPELIB_DATA_SOURCE_DIR}/*.txt"
)

add_custom_target(TypeLibData ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TYPELIB_DATA_BINARY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TYPELIB_STANDARD_FILES}
        "${TYPELIB_DATA_BINARY_DIR}"
    SOURCES ${TYPELIB_STANDARD_FILES}
    COMMENT "Copying TypeLib runtime data"
)

include(GNUInstallDirs)
install(FILES ${TYPELIB_STANDARD_FILES}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/hexedit/typelib"
)

set(TYPELIB_DATA_BINARY_DIR "${TYPELIB_DATA_BINARY_DIR}" PARENT_SCOPE)
