# gif-h (Charlie Tangora, public domain) with local palette optimizations.
# Its own static library because two independent layers encode GIFs: core
# (ScreenCapture screenshots) and the optional recording library (animated
# GIF encoder). Owned by neither, it keeps core buildable with
# ENABLE_RECORDING=OFF and avoids a core <-> recording link cycle.
cmake_minimum_required(VERSION 3.16)
project(gif LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(gif STATIC gif.cpp)
# Consumers include it as "3rdparty/gif/gif.h"
target_include_directories(gif PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
