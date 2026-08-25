cmake_minimum_required(VERSION 3.16)
project(cmos-editor-poc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(cmos-editor-poc main.cpp)
target_link_libraries(cmos-editor-poc PRIVATE Qt6::Widgets)
