# POC 056: a Vulkan swapchain on a Wayland surface, timed presents and screen times (Linux only).
# Standalone: cmake -S tools/poc/056-wayland-vulkan-presenter -B <build> -G Ninja
cmake_minimum_required(VERSION 3.20)
project(poc054 C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(WL_PROTOCOLS "/usr/share/wayland-protocols" CACHE PATH "wayland-protocols XML folder")

set(GEN ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${GEN})
set(PROTOCOL_SOURCES)
foreach(xml stable/xdg-shell/xdg-shell.xml stable/presentation-time/presentation-time.xml
        staging/commit-timing/commit-timing-v1.xml staging/fifo/fifo-v1.xml)
    get_filename_component(name ${xml} NAME_WE)
    add_custom_command(
        OUTPUT ${GEN}/${name}-client.h ${GEN}/${name}.c
        COMMAND ${WAYLAND_SCANNER} client-header ${WL_PROTOCOLS}/${xml} ${GEN}/${name}-client.h
        COMMAND ${WAYLAND_SCANNER} private-code ${WL_PROTOCOLS}/${xml} ${GEN}/${name}.c
        DEPENDS ${WL_PROTOCOLS}/${xml})
    list(APPEND PROTOCOL_SOURCES ${GEN}/${name}.c ${GEN}/${name}-client.h)
endforeach()

add_executable(wlvk cpp/wlvk.cpp ${PROTOCOL_SOURCES})
target_include_directories(wlvk PRIVATE ${GEN})
target_link_libraries(wlvk PRIVATE Vulkan::Vulkan PkgConfig::WAYLAND pthread)
target_compile_options(wlvk PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# Questions 3 and 4: GL -> Vulkan through external memory, inside a Qt window (needs Qt 6 with private Gui headers)
find_package(Qt6 QUIET COMPONENTS Gui Widgets)
if(Qt6_FOUND)
    add_executable(qtvk cpp/qtvk.cpp ${GEN}/presentation-time.c ${GEN}/presentation-time-client.h)
    target_include_directories(qtvk PRIVATE ${GEN})
    target_link_libraries(qtvk PRIVATE Qt6::Widgets Qt6::Gui Qt6::GuiPrivate Vulkan::Vulkan PkgConfig::WAYLAND pthread)
    target_compile_options(qtvk PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()
