# DependencyCheck.cmake - Automatic dependency detection and installation
# This module checks for required dependencies and offers to install them

# ========================================
# vcpkg Integration (for MSVC builds)
# ========================================
function(setup_vcpkg)
    if(NOT WIN32 OR NOT MSVC)
        return()
    endif()
    
    # Check if vcpkg toolchain is already set
    if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        message(STATUS "[DependencyCheck] vcpkg toolchain already configured")
        return()
    endif()
    
    # Try to find vcpkg
    set(VCPKG_SEARCH_PATHS
        "$ENV{VCPKG_ROOT}"
        "C:/vcpkg"
        "C:/tools/vcpkg"
        "$ENV{USERPROFILE}/vcpkg"
    )
    
    foreach(VCPKG_PATH ${VCPKG_SEARCH_PATHS})
        if(EXISTS "${VCPKG_PATH}/scripts/buildsystems/vcpkg.cmake")
            set(VCPKG_ROOT "${VCPKG_PATH}" CACHE PATH "vcpkg root directory" FORCE)
            message(STATUS "[DependencyCheck] Found vcpkg at: ${VCPKG_ROOT}")
            
            # Auto-set OPENSSL_ROOT_DIR for vcpkg static triplet
            if(VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static" AND 
               EXISTS "${VCPKG_ROOT}/installed/x64-windows-static/include/openssl/ssl.h")
                if(NOT DEFINED OPENSSL_ROOT_DIR)
                    set(OPENSSL_ROOT_DIR "${VCPKG_ROOT}/installed/x64-windows-static" CACHE PATH "OpenSSL root directory" FORCE)
                    message(STATUS "[DependencyCheck] Auto-set OPENSSL_ROOT_DIR to: ${OPENSSL_ROOT_DIR}")
                endif()
            elseif(EXISTS "${VCPKG_ROOT}/installed/x64-windows/include/openssl/ssl.h")
                if(NOT DEFINED OPENSSL_ROOT_DIR)
                    set(OPENSSL_ROOT_DIR "${VCPKG_ROOT}/installed/x64-windows" CACHE PATH "OpenSSL root directory" FORCE)
                    message(STATUS "[DependencyCheck] Auto-set OPENSSL_ROOT_DIR to: ${OPENSSL_ROOT_DIR}")
                endif()
            endif()
            
            return()
        endif()
    endforeach()
    
    message(WARNING "[DependencyCheck] vcpkg not found. For MSVC builds with WebAPI, install vcpkg from https://github.com/microsoft/vcpkg")
endfunction()

# ========================================
# OpenSSL Detection with Auto-install Hints
# ========================================
function(check_openssl_dependency)
    find_package(OpenSSL QUIET)
    
    if(OpenSSL_FOUND)
        message(STATUS "[DependencyCheck] OpenSSL found: ${OPENSSL_VERSION}")
        return()
    endif()
    
    # Provide installation hints based on platform/compiler
    if(WIN32)
        if(MSVC)
            message(STATUS "[DependencyCheck] OpenSSL not found for MSVC build")
            
            # Check if vcpkg is available
            if(DEFINED VCPKG_ROOT AND EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
                # Check if OpenSSL is installed in vcpkg (static triplet preferred)
                if(EXISTS "${VCPKG_ROOT}/installed/x64-windows-static/include/openssl/ssl.h")
                    message(STATUS "[DependencyCheck] OpenSSL available in vcpkg (static), but toolchain may not be set")
                    message(STATUS "[DependencyCheck] Add: -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static")
                elseif(EXISTS "${VCPKG_ROOT}/installed/x64-windows/include/openssl/ssl.h")
                    message(STATUS "[DependencyCheck] OpenSSL available in vcpkg (dynamic), but toolchain may not be set")
                    message(STATUS "[DependencyCheck] Add: -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
                else()
                    message(STATUS "[DependencyCheck] vcpkg found but OpenSSL not installed")
                    message(STATUS "[DependencyCheck] Run: vcpkg install openssl:x64-windows-static")
                endif()
            else()
                message(WARNING 
                    "[DependencyCheck] For MSVC builds, install vcpkg and OpenSSL:\n"
                    "  1. git clone https://github.com/microsoft/vcpkg C:/vcpkg\n"
                    "  2. C:/vcpkg/bootstrap-vcpkg.bat\n"
                    "  3. vcpkg install openssl:x64-windows-static\n"
                    "  4. vcpkg integrate install  (run as Administrator)\n"
                    "  5. Add -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static"
                )
            endif()
        elseif(MINGW)
            message(STATUS "[DependencyCheck] OpenSSL not found for MinGW build")
            message(WARNING 
                "[DependencyCheck] For MinGW builds, install OpenSSL via MSYS2:\n"
                "  pacman -S mingw-w64-x86_64-openssl"
            )
        endif()
    elseif(APPLE)
        message(WARNING 
            "[DependencyCheck] OpenSSL not found. Install via Homebrew:\n"
            "  brew install openssl"
        )
    else()
        message(WARNING 
            "[DependencyCheck] OpenSSL not found. Install via package manager:\n"
            "  apt-get install libssl-dev  # Debian/Ubuntu\n"
            "  dnf install openssl-devel   # Fedora"
        )
    endif()
endfunction()

# ========================================
# CMake Version Check
# ========================================
function(check_cmake_version)
    if(CMAKE_VERSION VERSION_LESS "3.16")
        message(FATAL_ERROR 
            "[DependencyCheck] CMake 3.16+ required, found ${CMAKE_VERSION}\n"
            "Download from: https://cmake.org/download/"
        )
    endif()
endfunction()

# ========================================
# Python Detection with Auto-install Hints
# ========================================
function(check_python_dependency)
    # Static Python builds from source don't need system Python detection
    # Only check for MinGW which requires MSYS2's Python
    
    if(WIN32 AND MINGW)
        # MinGW uses MSYS2's Python (dynamic)
        find_package(Python3 QUIET COMPONENTS Development)
        
        if(Python3_FOUND)
            message(STATUS "[DependencyCheck] MSYS2 Python found: ${Python3_VERSION}")
        else()
            message(WARNING 
                "[DependencyCheck] Python not found for MinGW build\n"
                "[DependencyCheck] Install via MSYS2 (run in MSYS2 MinGW64 shell):\n"
                "  pacman -S mingw-w64-x86_64-python\n"
                "  pacman -S mingw-w64-x86_64-pybind11\n"
                "  pacman -S mingw-w64-x86_64-python-pip"
            )
        endif()
    else()
        # Static build: CPython is built from source, no system dependency
        message(STATUS "[DependencyCheck] Python will be built from source (static)")
        message(STATUS "[DependencyCheck] Build requirements: C compiler, autotools (Unix)")
    endif()
endfunction()

# ========================================
# Run All Checks
# ========================================
macro(run_dependency_checks)
    check_cmake_version()
    
    if(WIN32 AND MSVC)
        setup_vcpkg()
    endif()
    
    if(ENABLE_WEBAPI_AUTOMATION)
        check_openssl_dependency()
    endif()
    
    if(ENABLE_PYTHON_AUTOMATION)
        check_python_dependency()
    endif()
endmacro()
