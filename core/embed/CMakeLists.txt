cmake_minimum_required(VERSION 3.16)

project(unrealng_embed LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EMBED_SOURCES
    src/embed_api.cpp
    src/embed_video.h
    src/embed_video.cpp
    src/embed_audio.h
)

if (APPLE)
    list(APPEND EMBED_SOURCES src/embed_audio.mm)
else()
    list(APPEND EMBED_SOURCES src/embed_audio.cpp)
endif()

add_library(unrealng_embed STATIC ${EMBED_SOURCES})

target_include_directories(unrealng_embed PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(unrealng_embed PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/core/src
    ${CMAKE_SOURCE_DIR}/core/automation
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(unrealng_embed PRIVATE
    unrealng::core
)

if (ENABLE_AUTOMATION)
    target_compile_definitions(unrealng_embed PRIVATE ENABLE_AUTOMATION)
    target_link_libraries(unrealng_embed PRIVATE unrealng::automation)
endif()

if (ENABLE_RECORDING)
    target_compile_definitions(unrealng_embed PRIVATE ENABLE_RECORDING)
    target_include_directories(unrealng_embed PRIVATE ${CMAKE_SOURCE_DIR}/core/recording/src)
endif()

if (APPLE)
    target_link_libraries(unrealng_embed PRIVATE
        "-framework Foundation"
        "-framework CoreFoundation"
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework AVFoundation"
    )
endif()
