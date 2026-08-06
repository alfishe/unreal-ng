cmake_minimum_required(VERSION 3.19)
project(unreal-ng-ui VERSION 0.9.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(unreal-ng-ui
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ScreenWidget.cpp
    src/ScreenWidget.h
    src/StatusIndicator.cpp
    src/StatusIndicator.h
    resources/assets.qrc
)

target_include_directories(unreal-ng-ui PRIVATE src)
target_link_libraries(unreal-ng-ui PRIVATE Qt6::Widgets)

set_target_properties(unreal-ng-ui PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_GUI_IDENTIFIER org.unreal-ng.ui
)

install(TARGETS unreal-ng-ui
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)
