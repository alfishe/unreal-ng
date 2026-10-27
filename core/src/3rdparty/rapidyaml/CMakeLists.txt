# rapidyaml 0.9.0 (single-header YAML / JSON parser, MIT): its definitions compile in one translation unit,
# ryml_impl.cpp. A library of its own, so the core and core/controllers (which uses no core symbols) both link it.
# The vendored code is not warning-clean under -Wall -Wextra -Werror and must not be force-fed stdafx.h
cmake_minimum_required(VERSION 3.16)
project(rapidyaml LANGUAGES CXX)

add_library(rapidyaml STATIC ryml_impl.cpp)
set_target_properties(rapidyaml PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
if (MSVC)
    target_compile_options(rapidyaml PRIVATE /w)
else()
    target_compile_options(rapidyaml PRIVATE -w)
endif()
# Consumers include it as "3rdparty/rapidyaml/ryml_all.hpp"
target_include_directories(rapidyaml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
