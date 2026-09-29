# Headless ctprobe harness on the xpeccy-plus core. Compiles libxpeccy straight
# from the (unmodified) checkout; no Qt.
cmake_minimum_required(VERSION 3.16)
project(ctharness C CXX)

set(XPECCY_DIR "" CACHE PATH "xpeccy-plus checkout")
if(NOT XPECCY_DIR)
    message(FATAL_ERROR "set -DXPECCY_DIR=<xpeccy-plus checkout>")
endif()
set(XSRC ${XPECCY_DIR}/src/libxpeccy)

file(GLOB_RECURSE CORE_C ${XSRC}/*.c)
file(GLOB_RECURSE CORE_CXX ${XSRC}/*.cpp)

# same flags as the upstream CMakeLists (-std=gnu99 / c++14, -O2, little endian)
set(CMAKE_C_FLAGS "-std=gnu99 -O2 -w ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "-std=c++14 -O2 -w ${CMAKE_CXX_FLAGS}")
add_definitions(-DWORDS_LITTLE_ENDIAN -DHAVEZLIB)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_library(xpeccycore STATIC ${CORE_C} ${CORE_CXX})
target_include_directories(xpeccycore PUBLIC ${XPECCY_DIR}/src)
target_link_libraries(xpeccycore PUBLIC Threads::Threads ZLIB::ZLIB)

add_executable(ctharness ctharness.c)
target_link_libraries(ctharness xpeccycore)
