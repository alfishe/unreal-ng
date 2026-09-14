# tools/poc/CMakeLists.txt — parent for all PoC research tools.
#
# Each PoC lives under tools/poc/<name>/ and is enabled by -DBUILD_POC=ON
# at the top-level CMakeLists.txt.
add_subdirectory(01-ttd-compression/cpp)
#add_subdirectory(02-ttd-gui)
add_subdirectory(011-ttd-v2-capture-analysis)
# 015-opl4-synthesis is now also a dependency of the emulator core (see
# core/src/CMakeLists.txt): when building from the root, the opl4 target
# already exists by the time we get here — skip the directory to avoid
# duplicate targets. opl4tests stays available as an explicit target
# (ninja opl4tests); standalone PoC builds are unaffected.
if (NOT TARGET opl4)
    add_subdirectory(015-opl4-synthesis)
endif()
