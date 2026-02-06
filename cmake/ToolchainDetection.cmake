# ============================================================================
# ToolchainDetection.cmake - Windows toolchain detection and dependency setup
# ============================================================================
# MUTUAL EXCLUSION POLICY:
#   - MSVC  → vcpkg dependencies ONLY
#   - MinGW → MSYS2/pacman dependencies ONLY
#
# vcpkg libraries are MSVC-compiled and binary-incompatible with MinGW.
# This module enforces strict separation to prevent linker errors.
#
# Include this BEFORE project() in root CMakeLists.txt.
# Call validate_toolchain_dependencies() AFTER project().
#
# Sets:
#   UNREAL_USING_MINGW      - TRUE if MinGW toolchain detected
#   UNREAL_USING_MSVC       - TRUE if MSVC toolchain detected
#   CMAKE_TOOLCHAIN_FILE    - Set to vcpkg for MSVC builds only
#   VCPKG_TARGET_TRIPLET    - Auto-detected for MSVC builds
# ============================================================================

# ============================================================================
# PHASE 3: Post-project() Validation Function (must be defined before return)
# ============================================================================
# Call validate_toolchain_dependencies() after project() to verify the
# compiler matches our detection and dependencies are correctly configured.
# This function is defined first because it must be available on all platforms.

function(validate_toolchain_dependencies)
    if(NOT WIN32)
        return()
    endif()
    
    # Now CMAKE_CXX_COMPILER_ID is available
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # --------------------------------------------------------
        # FATAL ERROR: vcpkg with MinGW/Clang
        # --------------------------------------------------------
        if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
            message(FATAL_ERROR
                "\n"
                "========================================\n"
                "FATAL: Toolchain Mismatch Detected!\n"
                "========================================\n"
                "Compiler: ${CMAKE_CXX_COMPILER_ID} (MinGW/MSYS2)\n"
                "Toolchain: vcpkg (MSVC-only)\n"
                "\n"
                "vcpkg libraries are compiled with MSVC and are\n"
                "binary-incompatible with MinGW/GCC.\n"
                "\n"
                "FIX: Delete build directory and reconfigure.\n"
                "     Do NOT set CMAKE_TOOLCHAIN_FILE to vcpkg.\n"
                "========================================\n"
            )
        endif()
        
        # Verify OpenSSL is from MSYS2, not vcpkg
        if(OPENSSL_CRYPTO_LIBRARY MATCHES "vcpkg")
            message(FATAL_ERROR
                "\n"
                "========================================\n"
                "FATAL: OpenSSL from vcpkg with MinGW!\n"
                "========================================\n"
                "Found: ${OPENSSL_CRYPTO_LIBRARY}\n"
                "\n"
                "FIX: Delete build directory and reconfigure.\n"
                "     Install MSYS2 OpenSSL:\n"
                "     pacman -S mingw-w64-x86_64-openssl\n"
                "========================================\n"
            )
        endif()
        
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # --------------------------------------------------------
        # FATAL ERROR: MSYS2 paths with MSVC
        # --------------------------------------------------------
        if(OPENSSL_ROOT_DIR MATCHES "msys64|mingw")
            message(FATAL_ERROR
                "\n"
                "========================================\n"
                "FATAL: MSYS2 OpenSSL with MSVC!\n"
                "========================================\n"
                "Found: ${OPENSSL_ROOT_DIR}\n"
                "\n"
                "FIX: Delete build directory and reconfigure.\n"
                "     Install vcpkg OpenSSL:\n"
                "     vcpkg install openssl:x64-windows-static\n"
                "========================================\n"
            )
        endif()
    endif()
    
    message(STATUS "Toolchain validation: OK")
endfunction()

# Early return for non-Windows platforms (function already defined above)
if(NOT WIN32)
    return()
endif()

# ============================================================================
# PHASE 1: Early Toolchain Detection (before project())
# ============================================================================
# CMAKE_CXX_COMPILER_ID is not available yet, so we detect from hints.
# MSYS2 supports both GCC and LLVM/Clang - both use pacman packages.

set(UNREAL_USING_MINGW FALSE)
set(UNREAL_USING_MSVC FALSE)

# Check C++ compiler path (may be set by IDE or -D flag)
# MSYS2 uses: g++, c++, clang++ (all are valid MinGW compilers)
if(CMAKE_CXX_COMPILER)
    if(CMAKE_CXX_COMPILER MATCHES "mingw|msys" OR 
       CMAKE_CXX_COMPILER MATCHES "g\\+\\+" OR
       CMAKE_CXX_COMPILER MATCHES "c\\+\\+\\.exe" OR
       CMAKE_CXX_COMPILER MATCHES "clang\\+\\+")
        set(UNREAL_USING_MINGW TRUE)
    elseif(CMAKE_CXX_COMPILER MATCHES "cl\\.exe")
        set(UNREAL_USING_MSVC TRUE)
    endif()
endif()

# Also check C compiler (CLion often uses cc.exe)
# MSYS2 uses: gcc, cc, clang
if(NOT UNREAL_USING_MINGW AND CMAKE_C_COMPILER)
    if(CMAKE_C_COMPILER MATCHES "mingw|msys" OR 
       CMAKE_C_COMPILER MATCHES "gcc" OR
       CMAKE_C_COMPILER MATCHES "cc\\.exe" OR
       CMAKE_C_COMPILER MATCHES "clang")
        set(UNREAL_USING_MINGW TRUE)
    elseif(CMAKE_C_COMPILER MATCHES "cl\\.exe")
        set(UNREAL_USING_MSVC TRUE)
    endif()
endif()

# Check environment variables - CLion/IDEs set CC/CXX
if(NOT UNREAL_USING_MINGW AND NOT UNREAL_USING_MSVC)
    if(DEFINED ENV{CC})
        if("$ENV{CC}" MATCHES "mingw|msys|gcc|cc|clang")
            set(UNREAL_USING_MINGW TRUE)
        elseif("$ENV{CC}" MATCHES "cl\\.exe")
            set(UNREAL_USING_MSVC TRUE)
        endif()
    endif()
endif()

# Check make program (Ninja from MSYS2 vs Visual Studio)
if(NOT UNREAL_USING_MINGW AND CMAKE_MAKE_PROGRAM MATCHES "mingw|msys")
    set(UNREAL_USING_MINGW TRUE)
endif()

# Fresh build detection: check if PATH contains MSYS2/MinGW before MSVC
# This is critical for fresh builds where CMAKE_C_COMPILER is not yet set
if(NOT UNREAL_USING_MINGW AND NOT UNREAL_USING_MSVC)
    if(DEFINED ENV{PATH})
        # Convert PATH to lowercase for reliable matching
        string(TOLOWER "$ENV{PATH}" _path_lower)
        # Check if MSYS2/MinGW is in PATH (before cl.exe might also be found)
        if(_path_lower MATCHES "msys64|mingw")
            set(UNREAL_USING_MINGW TRUE)
        endif()
    endif()
endif()

# Default to MSVC if no detection succeeded (Windows default)
if(NOT UNREAL_USING_MINGW AND NOT UNREAL_USING_MSVC)
    set(UNREAL_USING_MSVC TRUE)
endif()

# ============================================================================
# PHASE 2: Configure Dependency Sources (mutually exclusive)
# ============================================================================

if(UNREAL_USING_MINGW)
    message(STATUS "========================================")
    message(STATUS "Toolchain: MinGW (MSYS2)")
    message(STATUS "Dependencies: MSYS2 pacman packages")
    message(STATUS "========================================")
    
    # --------------------------------------------------------
    # ENFORCE: No vcpkg with MinGW
    # --------------------------------------------------------
    if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        message(STATUS "  [!] Clearing vcpkg toolchain (binary-incompatible with MinGW)")
        unset(CMAKE_TOOLCHAIN_FILE CACHE)
        unset(CMAKE_TOOLCHAIN_FILE)
    endif()
    
    # Clear any vcpkg-related variables
    unset(VCPKG_TARGET_TRIPLET CACHE)
    unset(VCPKG_ROOT CACHE)
    
    # --------------------------------------------------------
    # ENFORCE: Clear ALL cached OpenSSL paths (may be from vcpkg)
    # --------------------------------------------------------
    unset(OPENSSL_ROOT_DIR CACHE)
    unset(OPENSSL_INCLUDE_DIR CACHE)
    unset(OPENSSL_CRYPTO_LIBRARY CACHE)
    unset(OPENSSL_SSL_LIBRARY CACHE)
    unset(OPENSSL_LIBRARIES CACHE)
    unset(OPENSSL_VERSION CACHE)
    unset(OPENSSL_FOUND CACHE)
    unset(OpenSSL_FOUND CACHE)
    unset(LIB_EAY_DEBUG CACHE)
    unset(LIB_EAY_RELEASE CACHE)
    unset(SSL_EAY_DEBUG CACHE)
    unset(SSL_EAY_RELEASE CACHE)
    
    # --------------------------------------------------------
    # Set MSYS2 paths
    # --------------------------------------------------------
    if(EXISTS "C:/msys64/mingw64/include/openssl/ssl.h")
        set(OPENSSL_ROOT_DIR "C:/msys64/mingw64" CACHE PATH "OpenSSL root" FORCE)
        set(OPENSSL_INCLUDE_DIR "C:/msys64/mingw64/include" CACHE PATH "OpenSSL include" FORCE)
        message(STATUS "  OpenSSL: C:/msys64/mingw64 (MSYS2)")
    else()
        message(WARNING "  [!] MSYS2 OpenSSL not found!")
        message(WARNING "      Install: pacman -S mingw-w64-x86_64-openssl")
    endif()

elseif(UNREAL_USING_MSVC)
    message(STATUS "========================================")
    message(STATUS "Toolchain: MSVC (Visual Studio)")
    message(STATUS "Dependencies: vcpkg")
    message(STATUS "========================================")
    
    # --------------------------------------------------------
    # ENFORCE: Clear MSYS2 paths if present
    # --------------------------------------------------------
    if(OPENSSL_ROOT_DIR MATCHES "msys64|mingw")
        message(STATUS "  [!] Clearing MSYS2 OpenSSL paths (binary-incompatible with MSVC)")
        unset(OPENSSL_ROOT_DIR CACHE)
        unset(OPENSSL_INCLUDE_DIR CACHE)
        unset(OPENSSL_CRYPTO_LIBRARY CACHE)
        unset(OPENSSL_SSL_LIBRARY CACHE)
    endif()
    
    # --------------------------------------------------------
    # Auto-detect vcpkg
    # --------------------------------------------------------
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        if(EXISTS "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain" FORCE)
            message(STATUS "  vcpkg: C:/vcpkg")
        elseif(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain" FORCE)
            message(STATUS "  vcpkg: $ENV{VCPKG_ROOT}")
        else()
            message(WARNING "  [!] vcpkg not found!")
            message(WARNING "      Install from: https://github.com/microsoft/vcpkg")
        endif()
    endif()
    
    # --------------------------------------------------------
    # Auto-detect vcpkg triplet
    # --------------------------------------------------------
    if(CMAKE_TOOLCHAIN_FILE AND NOT DEFINED VCPKG_TARGET_TRIPLET)
        get_filename_component(_VCPKG_ROOT "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
        get_filename_component(_VCPKG_ROOT "${_VCPKG_ROOT}" DIRECTORY)
        get_filename_component(_VCPKG_ROOT "${_VCPKG_ROOT}" DIRECTORY)
        if(EXISTS "${_VCPKG_ROOT}/installed/x64-windows-static")
            set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "vcpkg triplet" FORCE)
            message(STATUS "  Triplet: x64-windows-static")
        elseif(EXISTS "${_VCPKG_ROOT}/installed/x64-windows")
            set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "vcpkg triplet" FORCE)
            message(STATUS "  Triplet: x64-windows")
        endif()
    endif()
endif()
