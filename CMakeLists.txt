cmake_minimum_required(VERSION 3.16)

project(qexed VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_subdirectory(HexView)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        bookmarkdialog.h
        bookmarkdialog.cpp
        colourpickerwidget.h
        colourpickerwidget.cpp
        datatypecombobox.h
        datatypecombobox.cpp
        gotodialog.cpp
        gotodialog.h
        finddialog.cpp
        finddialog.h
        settings.cpp
        settings.h
        statusbar.cpp
        statusbar.h
        titlebar.cpp
        titlebar.h
        theme.cpp
        theme.h
)

if(WIN32)
    list(APPEND PROJECT_SOURCES qexed.rc)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(qexed
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        finddialog.ui
        resources.qrc
        gotodialog.ui
        gotodialog.cpp
        gotodialog.h
        bookmarks.ui
        preferences.ui
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET qexed APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(qexed SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(qexed
            ${PROJECT_SOURCES}
            resources.qrc
        )
    endif()
endif()

target_include_directories(qexed PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qexed PRIVATE Qt${QT_VERSION_MAJOR}::Widgets HexView)

if(WIN32)
    target_link_libraries(qexed PRIVATE dwmapi advapi32)
endif()

find_file(ADWAITA_QT6_PLUGIN adwaita.so
    PATH_SUFFIXES styles
    PATHS
        /usr/local/lib64/qt6/plugins
        /usr/local/lib/qt6/plugins
        /usr/lib64/qt6/plugins
        /usr/lib/qt6/plugins
)
if(ADWAITA_QT6_PLUGIN)
    get_filename_component(_adwaita_styles_dir "${ADWAITA_QT6_PLUGIN}" DIRECTORY)
    get_filename_component(_adwaita_plugin_dir "${_adwaita_styles_dir}" DIRECTORY)
    target_compile_definitions(qexed PRIVATE ADWAITA_QT6_PLUGIN_DIR="${_adwaita_plugin_dir}")
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.qexed)
endif()
set_target_properties(qexed PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS qexed
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(UNIX AND NOT APPLE)
    install(FILES qexed.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES qexed.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    )
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(qexed)
endif()
