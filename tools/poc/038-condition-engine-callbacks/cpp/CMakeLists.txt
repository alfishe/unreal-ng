# POC 038 (../README.md): standalone, not part of the emulator build.
#   cmake -S tools/poc/038-condition-engine-callbacks/cpp -B scratch/2026-10-26-poc-038/build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build scratch/2026-10-26-poc-038/build && scratch/2026-10-26-poc-038/build/poc038
cmake_minimum_required(VERSION 3.20)
project(poc038 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
set(LUA_DIR ${REPO}/core/automation/lua/lib/lua/lua-5.4.7/src)
file(GLOB LUA_SOURCES ${LUA_DIR}/*.c)
list(REMOVE_ITEM LUA_SOURCES ${LUA_DIR}/lua.c ${LUA_DIR}/luac.c)
add_library(poc038lua STATIC ${LUA_SOURCES})
target_include_directories(poc038lua PUBLIC ${LUA_DIR})
target_compile_definitions(poc038lua PRIVATE LUA_USE_POSIX)

add_executable(poc038 poc038.cpp)
target_include_directories(poc038 PRIVATE ${REPO}/core/automation/lua/lib/sol2/sol2-3.5.0/single/include)
target_link_libraries(poc038 PRIVATE poc038lua)

find_package(Python3 COMPONENTS Development.Embed)
if (Python3_Development.Embed_FOUND)
    target_compile_definitions(poc038 PRIVATE POC038_PYTHON=1)
    target_link_libraries(poc038 PRIVATE Python3::Python)
endif ()
