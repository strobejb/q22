project(HexView VERSION 0.1 LANGUAGES CXX)

qt_add_library(HexView STATIC
    hexview.h
    hexview.cpp
    hexviewbookmark.h
    hexviewbookmark.cpp
    hexviewdraw.cpp
    hexviewscroll.cpp
    hexviewmouse.cpp
    hexviewkeyboard.cpp
    hexviewclipboard.cpp
    hexviewfind.cpp
    seqbase.h
    sequence.h
    sequence.cpp
    seqbuf.cpp
)

target_include_directories(HexView PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                                   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../HexEdit)

target_compile_definitions(HexView PRIVATE NOMINMAX)
target_link_libraries(HexView PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
