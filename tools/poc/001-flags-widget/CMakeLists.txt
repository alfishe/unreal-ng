cmake_minimum_required(VERSION 3.16)
project(flags-widget-poc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(flags-widget-poc
    main.cpp
)

target_link_libraries(flags-widget-poc PRIVATE Qt6::Widgets)
