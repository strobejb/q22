set(HEXEDIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(HEXEDIT_RESOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)

set(PROJECT_SOURCES
        main.cpp
        version.h
        chrome/popupmenucallout.cpp
        chrome/popupmenucallout.h
        disasm/disasmpanel.h
        disasm/disasmpanel.cpp
        disasm/branchtarget.h
        disasm/pemetadata.h
        disasm/pemetadata.cpp
        disasm/elfmetadata.h
        disasm/elfmetadata.cpp
        disasm/codediscovery.h
        disasm/codediscovery.cpp
        disasm/demangle.h
        disasm/demangle.cpp
        fileproperties.h
        filestats/banner.cpp
        filestats/banner.h
        filestats/checksum.cpp
        filestats/datainterpreter.cpp
        filestats/entropy.h
        filestats/entropy.cpp
        filestats/properties.cpp
        filestats/resizegrip.cpp
        filestats/resizegrip.h
        filestats/sidepanel.cpp
        filestats/sidepanel.h
        filestats/strings.cpp
        filestats/stringscan.cpp
        filestats/stringscan.h
        filestats/widgets.cpp
        filestats/widgets.h
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        chrome/roundedlistwidget.cpp
        chrome/roundedlistwidget.h
        bookmarks/bookmarkpopup.h
        bookmarks/bookmarkpopup.cpp
        bookmarks/bookmarkactivation.h
        bookmarks/bookmarkactivation.cpp
        bookmarks/bookmarkcombo.h
        bookmarks/bookmarkcombo.cpp
        bookmarks/bookmarkstore.h
        bookmarks/bookmarkstore.cpp
        bookmarks/dlgbookmark.h
        bookmarks/dlgbookmark.cpp
        bookmarks/bookmarkcolourwidget.h
        bookmarks/bookmarkcolourwidget.cpp
        bookmarks/bookmarks.ui
        combos/datatypecombobox.h
        combos/datatypecombobox.cpp
        combos/valuecombobox.h
        combos/valuecombobox.cpp
        chrome/dialog-chrome.h
        chrome/dialog-chrome.cpp
        panels/dockpanelhost.h
        panels/dockpanelhost.cpp
        panels/dockpanelrow.h
        panels/dockpanelrow.cpp
        combos/menucombobox.h
        combos/menucombobox.cpp
        combos/statuscomboboxes.h
        combos/statuscomboboxes.cpp
        panels/gotopanel.cpp
        panels/gotopanel.h
        panels/gotopanel.ui
        panels/findpanel.cpp
        panels/findpanel.h
        panels/findpattern.cpp
        panels/findpattern.h
        panels/findpanel.ui
        settings/focusnavigation.cpp
        settings/focusnavigation.h
        palette/palettes.cpp
        palette/palettes.h
        palette/palettecolourpicker.cpp
        palette/palettecolourpicker.h
        palette/paletteswatch.cpp
        palette/paletteswatch.h
        palette/screencolorpicker.cpp
        palette/screencolorpicker.h
        settings/preferences.cpp
        settings/preferences.h
        settings/preferences.ui
        chrome/shadow-chrome.cpp
        chrome/shadow-chrome.h
        settings/slideoverlay.cpp
        settings/slideoverlay.h
        settings/settings.cpp
        settings/settings.h
        statusbar.cpp
        statusbar.h
        structview/structuredefinitionmanager.cpp
        structview/structuredefinitionmanager.h
        structview/structurebranchicons.h
        structview/structurecommentformatter.h
        structview/structuregriditemdelegate.cpp
        structview/structuregriditemdelegate.h
        structview/elfsemanticview.cpp
        structview/elfsemanticview.h
        structview/pesemanticview.cpp
        structview/pesemanticview.h
        structview/structuresemanticview.cpp
        structview/structuresemanticview.h
        structview/structuredisplayoptions.h
        structview/structuretypenameformatter.cpp
        structview/structuretypenameformatter.h
        structview/structuretreemodel.cpp
        structview/structuretreemodel.h
        structview/structurerenderengine.cpp
        structview/structurerenderengine.h
        structview/structurevaluebuilder.cpp
        structview/structurevaluebuilder.h
        structview/structureviewpanel.cpp
        structview/structureviewpanel.h
        themepicker.cpp
        themepicker.h
        chrome/titlebar.cpp
        chrome/titlebar.h
        chrome/windows-chrome.cpp
        chrome/windows-chrome.h
        chrome/linux-chrome.cpp
        chrome/linux-chrome.h
        settings/togglebuttongroup.cpp
        settings/togglebuttongroup.h
        theme.cpp
        theme.h
        dialogs/dlgexport.h
        dialogs/dlgexport.cpp
        dialogs/exportformat.h
        dialogs/exportformat.cpp
        dialogs/importformat.h
        dialogs/importformat.cpp
        dialogs/dlgcopyas.h
        dialogs/dlgcopyas.cpp
        dialogs/copyas.ui
        dialogs/dlgimport.h
        dialogs/dlgimport.cpp
        dialogs/dlgpastespecial.h
        dialogs/dlgpastespecial.cpp
        dialogs/pastespecial.ui
        dialogs/dlgabout.h
        dialogs/dlgabout.cpp
        dialogs/dlgprogress.h
        dialogs/dlgprogress.cpp
        settings/settingscard.h
        settings/settingscard.cpp
        settings/scrollhintoverlay.h
        settings/scrollhintoverlay.cpp
)

set(PROJECT_FORMS
        mainwindow.ui
        bookmarks/bookmarks.ui
        dialogs/copyas.ui
        dialogs/pastespecial.ui
        panels/findpanel.ui
        panels/gotopanel.ui
        settings/preferences.ui
)
list(TRANSFORM PROJECT_FORMS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/"
     OUTPUT_VARIABLE PROJECT_FORMS_ABSOLUTE)
source_group("Forms" FILES ${PROJECT_FORMS_ABSOLUTE})

if(WIN32)
    list(APPEND PROJECT_SOURCES resources/hexedit.rc)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(hexedit
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        resources/icons/actions/document-edit-symbolic.svg resources/icons/actions/edit-clear-symbolic.svg resources/icons/actions/user-trash-symbolic.svg
        resources/icons/actions/pen-to-square-solid-full.svg
        resources/icons/actions/edit-copy-symbolic.svg resources/icons/actions/edit-cut-symbolic.svg resources/icons/actions/edit-delete-symbolic.svg
        resources/icons/actions/help-about-symbolic.svg
        resources/icons/actions/hierarchy1.svg
        resources/icons/actions/bug.svg
        resources/icons/actions/media-playback-stop-symbolic.svg resources/icons/actions/media-playlist-repeat-symbolic.svg resources/icons/actions/stopcircle.svg
        resources/icons/actions/media-playback-start-symbolic.svg
        resources/icons/actions/go-last-symbolic.svg
        resources/icons/actions/downloads.svg
        resources/icons/actions/circle-repeat.svg
        resources/icons/actions/bookmark-star-add.svg
        resources/icons/actions/object-flip-vertical-symbolic.svg resources/icons/actions/view-fullscreen-symbolic.svg
        resources/q22-title.png
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET hexedit APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(hexedit SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(hexedit
            ${PROJECT_SOURCES}
        )
    endif()
endif()

file(GLOB_RECURSE HEXEDIT_RESOURCES CONFIGURE_DEPENDS
    "${HEXEDIT_RESOURCE_DIR}/icons/*.svg"
    "${HEXEDIT_RESOURCE_DIR}/palettes/*.palette"
    "${HEXEDIT_RESOURCE_DIR}/*.png"
)
qt_add_resources(hexedit "app_resources"
    PREFIX "/"
    BASE "${HEXEDIT_RESOURCE_DIR}"
    FILES ${HEXEDIT_RESOURCES}
)

set_source_files_properties(
    "${CMAKE_SOURCE_DIR}/src/causeway/strata/README.md"
    PROPERTIES QT_RESOURCE_ALIAS "README.md"
)
qt_add_resources(hexedit "strata_docs"
    PREFIX "/docs"
    FILES "${CMAKE_SOURCE_DIR}/src/causeway/strata/README.md"
)

target_include_directories(hexedit PRIVATE
    ${HEXEDIT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
)
set_target_properties(hexedit PROPERTIES
    AUTOUIC_SEARCH_PATHS "${HEXEDIT_SOURCE_DIR};${HEXEDIT_SOURCE_DIR}/bookmarks"
)
target_link_libraries(hexedit PRIVATE Qt${QT_VERSION_MAJOR}::Widgets HexView causeway capstone)
if(NOT WIN32 AND TARGET Qt${QT_VERSION_MAJOR}::DBus)
    target_link_libraries(hexedit PRIVATE Qt${QT_VERSION_MAJOR}::DBus)
    target_compile_definitions(hexedit PRIVATE HEXEDIT_HAVE_DBUS)
endif()
set_target_properties(hexedit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    OUTPUT_NAME q22
)

if(WIN32)
    target_link_libraries(hexedit PRIVATE dwmapi advapi32 dbghelp)
endif()

if(TARGET causewayData)
    add_dependencies(hexedit causewayData)
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
    target_compile_definitions(hexedit PRIVATE ADWAITA_QT6_PLUGIN_DIR="${_adwaita_plugin_dir}")
    set_target_properties(hexedit PROPERTIES BUILD_RPATH "${_adwaita_lib_dir}")
    target_link_libraries(hexedit PRIVATE
        "${_adwaita_lib_dir}/libadwaitaqt6.so"
        "${_adwaita_lib_dir}/libadwaitaqt6priv.so")
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER net.catch22.q22)
endif()
set_target_properties(hexedit PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS hexedit
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(UNIX AND NOT APPLE)
    install(FILES ${HEXEDIT_RESOURCE_DIR}/q22.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES ${HEXEDIT_RESOURCE_DIR}/q22.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
    )
    install(FILES ${HEXEDIT_RESOURCE_DIR}/q22.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME q22.png
    )
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(hexedit)
endif()
