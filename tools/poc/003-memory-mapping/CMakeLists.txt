cmake_minimum_required(VERSION 3.16)
project(memory-mapping-poc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(memory-mapping-poc main.cpp)
target_link_libraries(memory-mapping-poc PRIVATE Qt6::Widgets)
