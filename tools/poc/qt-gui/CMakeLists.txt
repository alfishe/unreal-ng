cmake_minimum_required(VERSION 3.19)
project(unreal-ng-ui VERSION 0.9.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# Platform-specific icon resources
set(APP_ICON_RESOURCE)
if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE unreal-ng.icns)
    set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app/unreal-ng.icns")
    set_source_files_properties(${APP_ICON_MACOS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set(APP_ICON_RESOURCE ${APP_ICON_MACOS})
elseif(WIN32)
    # Windows resource file for icon
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
        @ONLY
    )
    set(APP_ICON_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

qt_add_executable(unreal-ng-ui
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ScreenWidget.cpp
    src/ScreenWidget.h
    src/StatusIndicator.cpp
    src/StatusIndicator.h
    resources/assets.qrc
    ${APP_ICON_RESOURCE}
)

target_include_directories(unreal-ng-ui PRIVATE src)
target_link_libraries(unreal-ng-ui PRIVATE Qt6::Widgets)

set_target_properties(unreal-ng-ui PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_GUI_IDENTIFIER org.unreal-ng.ui
    MACOSX_BUNDLE_BUNDLE_NAME "unreal-ng"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE unreal-ng.icns
)

install(TARGETS unreal-ng-ui
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)
