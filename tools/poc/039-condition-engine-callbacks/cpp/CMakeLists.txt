# POC 039 (../README.md): standalone, not part of the emulator build.
#   cmake -S tools/poc/039-condition-engine-callbacks/cpp -B scratch/2026-10-26-poc-039/build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build scratch/2026-10-26-poc-039/build && scratch/2026-10-26-poc-039/build/poc039
cmake_minimum_required(VERSION 3.20)
project(poc039 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
set(LUA_DIR ${REPO}/core/automation/lua/lib/lua/lua-5.4.7/src)
file(GLOB LUA_SOURCES ${LUA_DIR}/*.c)
list(REMOVE_ITEM LUA_SOURCES ${LUA_DIR}/lua.c ${LUA_DIR}/luac.c)
add_library(poc039lua STATIC ${LUA_SOURCES})
target_include_directories(poc039lua PUBLIC ${LUA_DIR})
target_compile_definitions(poc039lua PRIVATE LUA_USE_POSIX)

add_executable(poc039 poc039.cpp)
target_include_directories(poc039 PRIVATE ${REPO}/core/automation/lua/lib/sol2/sol2-3.5.0/single/include)
target_link_libraries(poc039 PRIVATE poc039lua)

find_package(Python3 COMPONENTS Development.Embed)
if (Python3_Development.Embed_FOUND)
    target_compile_definitions(poc039 PRIVATE POC039_PYTHON=1)
    target_link_libraries(poc039 PRIVATE Python3::Python)
endif ()
