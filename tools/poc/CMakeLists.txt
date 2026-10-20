# tools/poc/CMakeLists.txt — parent for all PoC research tools.
#
# Each PoC lives under tools/poc/<name>/ and is enabled by -DBUILD_POC=ON
# at the top-level CMakeLists.txt.
add_subdirectory(010-ttd-compression/cpp)
#add_subdirectory(027-ttd-gui)
add_subdirectory(011-ttd-v2-capture-analysis)
# 015-opl4-synthesis is the standalone PoC of the chip library. The emulator core does NOT
# build this directory: it uses its own vendored copy in core/src/3rdparty/opl4 (declared in
# core/src/CMakeLists.txt). When that opl4 target already exists (root build), skip the PoC copy
# to avoid duplicate target names; standalone PoC builds are unaffected.
if (NOT TARGET opl4)
    add_subdirectory(015-opl4-synthesis)
endif()
# 032 needs libsdb: configure with -DUNREAL_STREAMDECK=ON as well.
if (TARGET sdb)
    add_subdirectory(032-streamdeck-jpeg-encoder)
endif ()
