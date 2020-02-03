cmake_minimum_required(VERSION 3.0.0)

# THIS HAS TO COME BEFORE THE PROJECT LINE
if (APPLE)
    # Only macOS 15 has std::filesystem libraries in CLang, so use brew installed GCC instead
    set(CMAKE_C_COMPILER "/usr/local/bin/gcc")
    set(CMAKE_CXX_COMPILER "/usr/local/bin/g++")
endif (APPLE)
# THIS HAS TO COME BEFORE THE PROJECT LINE

project (unreal)

add_subdirectory(core)
add_subdirectory(testclient)
