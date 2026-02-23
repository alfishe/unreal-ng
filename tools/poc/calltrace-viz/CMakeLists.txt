cmake_minimum_required(VERSION 3.20)
project(calltrace-viz-poc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)

add_executable(calltrace-viz-poc
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/memory16kbwidget.cpp
    src/memory16kbwidget.h
    src/syntheticdata.cpp
    src/syntheticdata.h
    resources/resources.qrc
)

target_link_libraries(calltrace-viz-poc PRIVATE Qt6::Widgets Qt6::Svg)
