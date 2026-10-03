# 05: the op list as an OpenGL 4.1 fragment shader (macOS: CGL, headless).
if(APPLE)
    add_executable(gl-render ${CMAKE_CURRENT_LIST_DIR}/gl-render.cpp)
    target_include_directories(gl-render PRIVATE ${EVE_COMMON})
    target_link_libraries(gl-render PRIVATE eve-lib-base "-framework OpenGL")
endif()
