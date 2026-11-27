# The linker (UNREAL_LLD): lld instead of the system's ld for gcc and clang off Apple, in every build without LTO.
# GNU ld linked core-tests in 39 s and unreal-qt in 19 s on the Linux build host (88 threads, binutils 2.42), lld 18 in
# 7 and 1.7 s; mold was no faster than lld (2026-11-27). On by itself when the compiler links with -fuse-ld=lld (ld.lld
# on the PATH: Ubuntu's lld package, MSYS2's mingw64 bin); without lld the system's linker stays.
# Not on macOS: ld64.lld linked core-tests in the same 43 s as Apple's ld (the time goes to the objects' debug
# information there). Not with MSVC (link.exe). Not with LTO: gcc's LTO needs its plugin in GNU ld, and the release
# packages, the only LTO builds, keep the linker they were measured with. -DUNREAL_LLD=OFF keeps the system's linker.
# Included by the root CMakeLists.txt and by unreal-qt/CMakeLists.txt, after Lto.cmake and before any target.
include_guard(GLOBAL)

option(UNREAL_LLD "Link with lld (gcc / clang off Apple, builds without LTO)" ON)

set(UNREAL_LLD_ACTIVE OFF)
if (UNREAL_LLD AND NOT APPLE AND NOT MSVC AND NOT (UNREAL_LTO AND UNREAL_LTO_SUPPORTED))
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-fuse-ld=lld" UNREAL_LLD_WORKS)
    if (UNREAL_LLD_WORKS)
        add_link_options(-fuse-ld=lld)
        set(UNREAL_LLD_ACTIVE ON)
        message(STATUS "Linker: lld")
    else ()
        message(STATUS "Linker: the system's (lld not found)")
    endif ()
endif ()
