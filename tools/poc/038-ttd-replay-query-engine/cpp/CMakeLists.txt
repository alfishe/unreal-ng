# POC 038: TTD window queries replayed on headless clones. Configure with -DBUILD_TTD_REPLAY_POC=ON, then
# tools/build/build.sh ttd-replay-poc. Not part of the emulator.
add_executable(ttd-replay-poc replaypoc.cpp)
target_link_libraries(ttd-replay-poc PRIVATE unrealng::core)
set_target_properties(ttd-replay-poc PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(ttd-replay-poc PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
if (APPLE)
    target_link_libraries(ttd-replay-poc PRIVATE pthread iconv)
elseif (UNIX)
    target_link_libraries(ttd-replay-poc PRIVATE pthread)
endif ()
add_dependencies(ttd-replay-poc runtime-data)
