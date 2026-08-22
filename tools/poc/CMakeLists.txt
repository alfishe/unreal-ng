# tools/poc/CMakeLists.txt — parent for all PoC research tools.
#
# Each PoC lives under tools/poc/<name>/ and is enabled by -DBUILD_POC=ON
# at the top-level CMakeLists.txt.
add_subdirectory(01-ttd-compression/cpp)
add_subdirectory(02-ttd-gui)
