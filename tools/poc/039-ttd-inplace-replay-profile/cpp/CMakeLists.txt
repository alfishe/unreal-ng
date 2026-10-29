# POC 039: where the in-place TTD replay spends its time. Configure with -DBUILD_TTD_INPLACE_PROFILE_POC=ON, then
# tools/build/build.sh ttd-inplace-profile. Not part of the emulator.
add_executable(ttd-inplace-profile inplaceprofile.cpp)
target_link_libraries(ttd-inplace-profile PRIVATE unrealng::core)
set_target_properties(ttd-inplace-profile PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(ttd-inplace-profile PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
if (APPLE)
    target_link_libraries(ttd-inplace-profile PRIVATE pthread iconv)
elseif (UNIX)
    target_link_libraries(ttd-inplace-profile PRIVATE pthread)
endif ()
add_dependencies(ttd-inplace-profile runtime-data)
