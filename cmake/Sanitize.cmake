# Sanitizer builds: -DSANITIZE=<list> instruments every target of the tree (compile and link).
#   address,undefined   AddressSanitizer + UndefinedBehaviorSanitizer (tools/sanitize/configure.sh asan)
#   thread              ThreadSanitizer (tools/sanitize/configure.sh tsan)
# A build directory of its own per kind: the instrumented objects never mix with a normal build.
# Guide: docs/testing/sanitizers.md
# Included by the root CMakeLists.txt and by unreal-qt/CMakeLists.txt (docker/linux/build.sh configures from there)
include_guard(GLOBAL)

set(SANITIZE "" CACHE STRING "Sanitizers to build with: address,undefined | thread | ... (empty: none)")

if (SANITIZE)
    if (MSVC)
        # MSVC has AddressSanitizer only
        if (NOT SANITIZE STREQUAL "address")
            message(FATAL_ERROR "SANITIZE=${SANITIZE}: MSVC supports SANITIZE=address only")
        endif ()
        add_compile_options(/fsanitize=address)
    else ()
        add_compile_options(-fsanitize=${SANITIZE} -fno-omit-frame-pointer)
        add_link_options(-fsanitize=${SANITIZE})
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # gcc reports false maybe-uninitialized inside libstdc++ (<regex>) once the sanitizers instrument it
            add_compile_options(-Wno-error=maybe-uninitialized)
            if (SANITIZE MATCHES "thread")
                # -Wtsan: TSan does not model atomic_thread_fence. The one fence (Memory::UpdateFeatureCache)
                # sits under a mutex TSan does see; keep the note, not the error
                add_compile_options(-Wno-error=tsan)
            endif ()
        endif ()
    endif ()
    message(STATUS "Sanitizers: ${SANITIZE}")
endif ()
