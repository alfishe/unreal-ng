# POC 034 (P-12): core/swsignature over a real collection. Configure with -DBUILD_SIGNATURE_POC=ON, then
# tools/build/build.sh signature-poc. Not part of the emulator.
add_executable(signature-poc sigpoc.cpp)
target_link_libraries(signature-poc PRIVATE unrealng::swsignature)
set_target_properties(signature-poc PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
