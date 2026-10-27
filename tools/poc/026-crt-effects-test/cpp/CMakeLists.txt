cmake_minimum_required(VERSION 3.16)
project(crt-effects-test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL)

# Include paths to the main project's widget sources
set(UNREAL_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../unreal-qt/src")

add_executable(crt-test
    crt_test.cpp
    ${UNREAL_SRC_DIR}/widgets/crtfilter.cpp
    ${UNREAL_SRC_DIR}/widgets/crtfilter.h
    ${UNREAL_SRC_DIR}/widgets/crtprofiles.cpp
    ${UNREAL_SRC_DIR}/widgets/crtprofiles.h
)

target_include_directories(crt-test PRIVATE
    ${UNREAL_SRC_DIR}
)

target_link_libraries(crt-test
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::OpenGL
)
