# unreal-disk - ZX Spectrum disks: the floppy track and sector model, the format drivers (TRD, SCL, FDI, UDI, TD0,
# DSK / EDSK, MGT, raw PC, MB-02, MDOS, HFE, SCP, hobeta), the block device seam of hard disks and cards, and the file systems
# (TR-DOS, +3DOS, iS-DOS / TASiS, ATM CP/M, MB-02, MDOS, FAT) behind one interface, DiskFs. Standard C++ only: the emulator's media manager
# and the standalone tools (zxdisk, zxasm, symconv) all build on it.
# Design: docs/inprogress/2026-10-10-unreal-disk/ (in the unreal-ng repository).
#
# Embedded use: add_subdirectory(3rdparty/unreal-disk) and link `unreal-disk`. Standalone:
#   cmake -S . -B build -G Ninja -DUNREAL_DISK_TESTS=ON && ninja -C build && ./build/unreal-disk-tests
cmake_minimum_required(VERSION 3.16)
project(unreal-disk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (DEFINED TESTS)
    set(UNREAL_DISK_TESTS_DEFAULT ${TESTS})
else()
    set(UNREAL_DISK_TESTS_DEFAULT OFF)
endif()
option(UNREAL_DISK_TESTS "Build unreal-disk-tests" ${UNREAL_DISK_TESTS_DEFAULT})
set(UNREAL_DISK_GOOGLETEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../lib/googletest" CACHE PATH "GoogleTest sources")

if (MSVC)
    set(UNREAL_DISK_WARNINGS /W4)
else()
    set(UNREAL_DISK_WARNINGS -Wall -Wextra)
endif()

add_library(unreal-disk STATIC
    src/diskimage.cpp
    src/trdoscatalog.cpp
    src/floppyformats.cpp
    src/trdosfs.cpp
    src/plus3fs.cpp
    src/isdosfs.cpp
    src/cpmfs.cpp
    src/mb01fs.cpp
    src/mb02fs.cpp
    src/mdosfs.cpp
    src/unicode.cpp
    src/subrangedevice.cpp
    src/fatvolumereader.cpp
    src/fatfs.cpp
    src/floppyblockdevice.cpp
    src/diskfs.cpp
    src/flux/flux_pll.cpp
    src/flux/mfm_decoder.cpp
    src/flux/mfm_encoder.cpp
    src/loaders/loader_dsk.cpp
    src/loaders/loader_fdi.cpp
    src/loaders/loader_hfe.cpp
    src/loaders/loader_hobeta.cpp
    src/loaders/loader_mb02.cpp
    src/loaders/loader_mdos.cpp
    src/loaders/loader_mgt.cpp
    src/loaders/loader_pro.cpp
    src/loaders/loader_rawpc.cpp
    src/loaders/loader_scl.cpp
    src/loaders/loader_scp.cpp
    src/loaders/loader_td0.cpp
    src/loaders/loader_trd.cpp
    src/loaders/loader_udi.cpp
)
target_include_directories(unreal-disk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(unreal-disk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(unreal-disk PRIVATE ${UNREAL_DISK_WARNINGS})

# Command-line tool: zxdisk, the files of any disk image (volumes, ls, cat, put, rm, mv, mkdir, attrib, label) through DiskFs
add_executable(zxdisk tools/zxdisk/zxdisk.cpp)
target_link_libraries(zxdisk PRIVATE unreal-disk)
target_compile_options(zxdisk PRIVATE ${UNREAL_DISK_WARNINGS})

set(UNREAL_DISK_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "unreal-disk source directory")
set(UNREAL_DISK_WARNINGS_LIST "${UNREAL_DISK_WARNINGS}" CACHE INTERNAL "unreal-disk warning options")

# unreal-disk-tests needs the gtest target: embedded in unreal-ng it comes from core/tests later, so the test target is
# defined at the end of the top-level directory; standalone this file adds GoogleTest itself
function(unrealdisk_add_tests)
    if (NOT TARGET gtest OR TARGET unreal-disk-tests)
        return()
    endif()
    file(GLOB UNREAL_DISK_TEST_SOURCES ${UNREAL_DISK_DIR}/tests/*.cpp)
    add_executable(unreal-disk-tests ${UNREAL_DISK_TEST_SOURCES})
    target_link_libraries(unreal-disk-tests PRIVATE unreal-disk gtest)
    target_include_directories(unreal-disk-tests PRIVATE ${UNREAL_DISK_DIR}/src ${UNREAL_DISK_DIR}/tests)
    get_filename_component(UNREAL_DISK_REPO_TESTDATA ${UNREAL_DISK_DIR}/../../../../testdata ABSOLUTE)
    target_compile_definitions(unreal-disk-tests PRIVATE UNREAL_DISK_TESTDATA_DIR="${UNREAL_DISK_REPO_TESTDATA}")
    target_compile_options(unreal-disk-tests PRIVATE ${UNREAL_DISK_WARNINGS_LIST})
    if (MINGW)
        # Runs without the MinGW runtime DLLs on the path, as core-tests does
        target_link_options(unreal-disk-tests PRIVATE -static)
    endif()
endfunction()

if (UNREAL_DISK_TESTS)
    if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        if (NOT TARGET gtest)
            set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
            add_subdirectory(${UNREAL_DISK_GOOGLETEST_DIR} ${CMAKE_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)
        endif()
        unrealdisk_add_tests()
        enable_testing()
        add_test(NAME unreal-disk-tests COMMAND unreal-disk-tests)
    elseif (TARGET gtest)
        unrealdisk_add_tests()
    else()
        cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL unrealdisk_add_tests)
    endif()
endif()
