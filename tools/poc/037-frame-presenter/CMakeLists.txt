# POC 037 frame-presenter: two shells (Qt 6, SDL3) over one lab (common/). Standalone, not part of the emulator build:
#
#   cmake -S tools/poc/037-frame-presenter -B scratch/2026-10-26-poc-037/build -G Ninja \
#         -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="<Qt6 prefix>;<SDL3 prefix>"
#   cmake --build scratch/2026-10-26-poc-037/build
cmake_minimum_required(VERSION 3.24)
project(poc037 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LAB common/lab.cpp)
if(APPLE)
    list(APPEND LAB common/presenter_metal.mm)
    set_source_files_properties(common/presenter_metal.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set(LAB_LIBS "-framework Metal" "-framework QuartzCore" "-framework AppKit")
elseif(WIN32)
    list(APPEND LAB common/presenter_dxgi.cpp)
    set(LAB_LIBS d3d11 dxgi d3dcompiler dwmapi gdi32)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

find_package(Qt6 COMPONENTS Gui OpenGL)
if(Qt6_FOUND)
    add_executable(poc037-qt qt/main.cpp ${LAB})
    target_link_libraries(poc037-qt PRIVATE Qt6::Gui Qt6::OpenGL ${LAB_LIBS})
endif()

find_package(SDL3 CONFIG)
if(SDL3_FOUND)
    add_executable(poc037-sdl sdl/main.cpp ${LAB})
    target_link_libraries(poc037-sdl PRIVATE SDL3::SDL3 ${LAB_LIBS})
endif()
