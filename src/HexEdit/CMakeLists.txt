set(HEXEDIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(HEXEDIT_RESOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        bookmarkdialog.h
        bookmarkdialog.cpp
        bookmarkcolourwidget.h
        bookmarkcolourwidget.cpp
        datatypecombobox.h
        datatypecombobox.cpp
        dockpanelhost.h
        dockpanelhost.cpp
        menucombobox.h
        menucombobox.cpp
        gotodialog.cpp
        gotodialog.h
        finddialog.cpp
        finddialog.h
        focusnavigation.cpp
        focusnavigation.h
        palettes.cpp
        palettes.h
        palettecolourpicker.cpp
        palettecolourpicker.h
        paletteswatch.cpp
        paletteswatch.h
        preferences.cpp
        preferences.h
        screencolorpicker.cpp
        screencolorpicker.h
        slideoverlay.cpp
        slideoverlay.h
        settings.cpp
        settings.h
        statusbar.cpp
        statusbar.h
        titlebar.cpp
        titlebar.h
        windows-chrome.cpp
        windows-chrome.h
        linux-chrome.cpp
        linux-chrome.h
        togglebuttongroup.cpp
        togglebuttongroup.h
        theme.cpp
        theme.h
        dlgexport.h
        dlgexport.cpp
        dlgcopyas.h
        dlgcopyas.cpp
        dlgimport.h
        dlgimport.cpp
        dlgpastespecial.h
        dlgpastespecial.cpp
        dlgabout.h
        dlgabout.cpp
        dlgprogress.h
        dlgprogress.cpp
        settingscard.h
        settingscard.cpp
)

if(WIN32)
    list(APPEND PROJECT_SOURCES resources/HexEdit.rc)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(HexEdit
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        finddialog.ui
        gotodialog.ui
        bookmarks.ui
        copyas.ui
        pastespecial.ui
        preferences.ui
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET HexEdit APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(HexEdit SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(HexEdit
            ${PROJECT_SOURCES}
        )
    endif()
endif()

file(GLOB_RECURSE HEXEDIT_RESOURCES CONFIGURE_DEPENDS
    "${HEXEDIT_RESOURCE_DIR}/icons/*.svg"
    "${HEXEDIT_RESOURCE_DIR}/icons/hicolor/index.theme"
    "${HEXEDIT_RESOURCE_DIR}/palettes/*.palette"
    "${HEXEDIT_RESOURCE_DIR}/HexEdit.png"
)
qt_add_resources(HexEdit "app_resources"
    PREFIX "/"
    BASE "${HEXEDIT_RESOURCE_DIR}"
    FILES ${HEXEDIT_RESOURCES}
)

target_include_directories(HexEdit PRIVATE
    ${HEXEDIT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(HexEdit PRIVATE Qt${QT_VERSION_MAJOR}::Widgets HexView)
if(TARGET Qt${QT_VERSION_MAJOR}::DBus)
    target_link_libraries(HexEdit PRIVATE Qt${QT_VERSION_MAJOR}::DBus)
    target_compile_definitions(HexEdit PRIVATE HEXEDIT_HAVE_DBUS)
endif()
set_target_properties(HexEdit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)

if(WIN32)
    target_link_libraries(HexEdit PRIVATE dwmapi advapi32)
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
    get_filename_component(_adwaita_qt6_dir "${_adwaita_plugin_dir}" DIRECTORY)
    get_filename_component(_adwaita_lib_dir "${_adwaita_qt6_dir}" DIRECTORY)
    target_compile_definitions(HexEdit PRIVATE ADWAITA_QT6_PLUGIN_DIR="${_adwaita_plugin_dir}")
    set_target_properties(HexEdit PROPERTIES BUILD_RPATH "${_adwaita_lib_dir}")
    target_link_libraries(HexEdit PRIVATE
        "${_adwaita_lib_dir}/libadwaitaqt6.so"
        "${_adwaita_lib_dir}/libadwaitaqt6priv.so")
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.HexEdit)
endif()
set_target_properties(HexEdit PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS HexEdit
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(UNIX AND NOT APPLE)
    install(FILES ${HEXEDIT_RESOURCE_DIR}/HexEdit.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES ${HEXEDIT_RESOURCE_DIR}/HexEdit.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    )
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(HexEdit)
endif()
