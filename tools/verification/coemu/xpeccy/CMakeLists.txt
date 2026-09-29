# Headless ctprobe harness on the upstream Xpeccy core. Compiles libxpeccy straight
# from the (unmodified) checkout; no Qt.
cmake_minimum_required(VERSION 3.16)
project(ctharness C CXX)

set(XPECCY_UPSTREAM_DIR "" CACHE PATH "upstream Xpeccy checkout")
if(NOT XPECCY_UPSTREAM_DIR)
    message(FATAL_ERROR "set -DXPECCY_UPSTREAM_DIR=<Xpeccy checkout>")
endif()
set(XSRC ${XPECCY_UPSTREAM_DIR}/src/libxpeccy)

file(GLOB_RECURSE CORE_C ${XSRC}/*.c)
file(GLOB_RECURSE CORE_CXX ${XSRC}/*.cpp)

# same flags as the upstream CMakeLists (-std=gnu99 / c++11, -O2, little endian, zlib for rzx)
set(CMAKE_C_FLAGS "-std=gnu99 -O2 -w ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "-std=c++11 -O2 -w ${CMAKE_CXX_FLAGS}")
add_definitions(-DWORDS_LITTLE_ENDIAN -DHAVEZLIB)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_library(xpeccycore STATIC ${CORE_C} ${CORE_CXX})
target_include_directories(xpeccycore PUBLIC ${XPECCY_UPSTREAM_DIR}/src)
target_link_libraries(xpeccycore PUBLIC Threads::Threads ZLIB::ZLIB ${CMAKE_DL_LIBS})

add_executable(ctharness ctharness.c)
target_link_libraries(ctharness xpeccycore)
