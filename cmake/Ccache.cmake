# Compiler cache (UNREAL_CCACHE, on by default when ccache 4.8 or newer is installed). Two layers:
# - each checkout (a worktree, a clone) writes only to its own cache, scratch/ccache: removing the worktree removes
#   its cache, nothing else to clean. It serves the rebuilds in that checkout (after a merge of master, a new build
#   directory, other options); size UNREAL_CCACHE_SIZE
# - the master cache, read only (ccache 4.14 or newer): the own cache of the master worktree each host keeps,
#   scratch/wt-ccache-master, which tools/build/ccache-master.sh builds at github/master after landings. A new
#   worktree finds there everything master compiled, so its first build compiles only what it changed. Path:
#   UNREAL_CCACHE_MASTER_DIR (default: that worktree of the main checkout; a Windows clone passes it); used when it
#   exists at configure time, and build.sh configures before every build
# Settings that let one checkout reuse what another compiled:
# - base_dir = the checkout's root: absolute paths in the command lines become relative, so the same file in
#   scratch/wt-a and scratch/wt-b hashes the same
# - hash_dir = false: the working directory stays out of the hash (with -g it is in by default); the debug info of a
#   reused object names the checkout that compiled it first
# - precompiled headers off (CMAKE_DISABLE_PRECOMPILE_HEADERS): a PCH holds the absolute paths of its headers and
#   ccache hashes its bytes, so a file compiled with one never hits in another checkout (core: about a third of all
#   compiles); ccache skipped those files altogether before. unreal_precompile_headers() below includes the header
#   in every source instead (-include, /FI), the same code as with the PCH
# - depend_mode = true: a miss runs the compiler once (the dependencies come from its -MD output), not the preprocessor
#   first and the compiler after
# They go on the launcher's command line (ccache KEY=VALUE compiler), above any ccache.conf. Before, ccache came from
# jsoncpp's CMakeLists.txt with none of these and one cache per user: 6-10 % hits on the build hosts.
# -DUNREAL_CCACHE=OFF compiles without it. Details: tools/build/README.md, section "Compiler cache".
# Included by the root CMakeLists.txt and by unreal-qt/CMakeLists.txt, after project() and before any target.
include_guard(GLOBAL)

option(UNREAL_CCACHE "Compile through ccache when it is installed" ON)
set(UNREAL_CCACHE_SIZE "10G" CACHE STRING "Size of the checkout's own ccache (scratch/ccache)")

if (UNREAL_CCACHE)
    find_program(UNREAL_CCACHE_PROGRAM ccache)
    if (UNREAL_CCACHE_PROGRAM)
        execute_process(COMMAND "${UNREAL_CCACHE_PROGRAM}" --version OUTPUT_VARIABLE UNREAL_CCACHE_VERSION_TEXT
                        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "version ([0-9]+\\.[0-9]+)" _ "${UNREAL_CCACHE_VERSION_TEXT}")
        set(UNREAL_CCACHE_VERSION "${CMAKE_MATCH_1}")
    endif ()
    if (NOT UNREAL_CCACHE_PROGRAM)
        message(STATUS "ccache: not found, compiling without it")
    elseif (UNREAL_CCACHE_VERSION VERSION_LESS 4.8)
        # KEY=VALUE on the command line came with 4.8
        message(STATUS "ccache: ${UNREAL_CCACHE_VERSION} is older than 4.8, compiling without it")
    else ()
        get_filename_component(UNREAL_CCACHE_BASE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
        set(UNREAL_CCACHE_LAUNCHER "${UNREAL_CCACHE_PROGRAM}" "base_dir=${UNREAL_CCACHE_BASE_DIR}"
            "cache_dir=${UNREAL_CCACHE_BASE_DIR}/scratch/ccache" "max_size=${UNREAL_CCACHE_SIZE}" "hash_dir=false"
            "depend_mode=true")
        set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
        set(UNREAL_CCACHE_ACTIVE ON)

        # The master cache: scratch/wt-ccache-master of the main checkout (git's common directory is its .git)
        if (NOT DEFINED UNREAL_CCACHE_MASTER_DIR)
            execute_process(COMMAND git rev-parse --path-format=absolute --git-common-dir
                            WORKING_DIRECTORY "${UNREAL_CCACHE_BASE_DIR}" OUTPUT_VARIABLE _common
                            ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
            if (_common)
                get_filename_component(_main "${_common}/.." ABSOLUTE)
                set(UNREAL_CCACHE_MASTER_DIR "${_main}/scratch/wt-ccache-master/scratch/ccache")
            endif ()
        endif ()
        set(_master "")
        if (UNREAL_CCACHE_MASTER_DIR AND IS_DIRECTORY "${UNREAL_CCACHE_MASTER_DIR}"
            AND NOT UNREAL_CCACHE_MASTER_DIR STREQUAL "${UNREAL_CCACHE_BASE_DIR}/scratch/ccache")
            if (UNREAL_CCACHE_VERSION VERSION_LESS 4.14)
                message(STATUS "ccache: ${UNREAL_CCACHE_VERSION} cannot read the master cache (4.14 can)")
            else ()
                file(TO_CMAKE_PATH "${UNREAL_CCACHE_MASTER_DIR}" _dir)
                string(REPLACE "%" "%25" _dir "${_dir}")
                string(REPLACE " " "%20" _dir "${_dir}")
                if (_dir MATCHES "^/")
                    set(_url "file:${_dir}")
                else ()
                    set(_url "file:///${_dir}")   # C:/...
                endif ()
                list(APPEND UNREAL_CCACHE_LAUNCHER "remote_storage=${_url} @layout=local read-only")
                set(_master ", master cache ${UNREAL_CCACHE_MASTER_DIR}")
            endif ()
        endif ()

        # Normal variables: they win over the cache entry jsoncpp's CMakeLists.txt forces to plain ccache
        foreach (lang C CXX OBJC OBJCXX)
            set(CMAKE_${lang}_COMPILER_LAUNCHER ${UNREAL_CCACHE_LAUNCHER})
        endforeach ()
        message(STATUS "ccache: ${UNREAL_CCACHE_VERSION}, own cache ${UNREAL_CCACHE_BASE_DIR}/scratch/ccache${_master}")
    endif ()
endif ()

# A target's precompiled header, or with ccache the same header included first in each of its C++ sources
function(unreal_precompile_headers target header)
    if (UNREAL_CCACHE_ACTIVE)
        if (MSVC)
            target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/FI${header}>")
        else ()
            target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${header}>")
        endif ()
    else ()
        target_precompile_headers(${target} PRIVATE ${header})
    endif ()
endfunction()
