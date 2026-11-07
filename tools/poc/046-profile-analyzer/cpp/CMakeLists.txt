# POC 046: the CPU time profile's cost per option and its attribution against L1. Configure with
# -DBUILD_PROFILE_ANALYZER_POC=ON, then tools/build/build.sh poc046-profile. Not part of the emulator.
add_executable(poc046-profile poc046.cpp)
target_link_libraries(poc046-profile PRIVATE unrealng::core)
set_target_properties(poc046-profile PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(poc046-profile PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
if (APPLE)
    target_link_libraries(poc046-profile PRIVATE pthread iconv)
elseif (UNIX)
    target_link_libraries(poc046-profile PRIVATE pthread)
endif ()
add_dependencies(poc046-profile runtime-data)
