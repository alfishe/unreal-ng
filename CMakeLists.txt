# CMake entry point - Unified build for all unreal-ng targets
cmake_minimum_required(VERSION 3.16)

message(">> Unreal CMakeLists.txt (Unified Build)")

# ========================================
# Windows toolchain and vcpkg setup (before project())
# ========================================
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ToolchainDetection.cmake)


# ========================================
# Build Options
# ========================================
option(TESTS "Enable tests" ON)
option(BENCHMARKS "Enable benchmarks" ON)
option(BUILD_QT_APPS "Build Qt-based applications (unreal-qt, unreal-screen-viewer)" ON)

# Automation options (enabled by default for full builds)
option(ENABLE_AUTOMATION "Enable automation features (Lua, Python, WebAPI, CLI)" ON)
option(ENABLE_LUA_AUTOMATION "Enable Lua automation" ON)
option(ENABLE_PYTHON_AUTOMATION "Enable Python automation" OFF)
option(ENABLE_WEBAPI_AUTOMATION "Enable WebAPI automation" ON)
option(ENABLE_CLI_AUTOMATION "Enable CLI automation" ON)

project(unreal)

# Validate toolchain/dependency compatibility (MSVC+vcpkg OR MinGW+pacman)
validate_toolchain_dependencies()

# ========================================
# C++ Standard Configuration (MUST be after project())
# ========================================
# Using C++20 for all compilers. MSVC requires it for designated initializers,
# and GCC/Clang benefit from additional C++20 features.
# Note: Using CACHE FORCE to ensure sub-projects don't override this.
set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "C++ standard required" FORCE)
message(STATUS "C++ Standard: C++20")

# Default to Release mode
if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif (NOT CMAKE_BUILD_TYPE)

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# Disable modern SDK warnings about insecurity for POSIX functions
# Also define WIN32_LEAN_AND_MEAN and NOMINMAX to prevent winsock/windows.h conflicts
if (MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	add_definitions(-DWIN32_LEAN_AND_MEAN)
	add_definitions(-DNOMINMAX)
endif (MSVC)

# Set the output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ========================================
# MinGW Static Linking (Windows only)
# ========================================
# This option statically links the MinGW runtime libraries (libgcc, libstdc++, libwinpthread)
# and OpenSSL into the executables, eliminating the need to distribute these DLLs.
option(MINGW_STATIC_LINKING "Statically link MinGW runtime and OpenSSL (Windows MinGW only)" OFF)

if (MINGW AND MINGW_STATIC_LINKING)
    message(STATUS "MinGW static linking: ENABLED")
    
    # Static link MinGW runtime libraries
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    
    # Static link libwinpthread
    # Note: -static links ALL libraries statically, which may cause issues with Qt
    # Instead, we link pthread statically via its specific flag
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Bstatic -lpthread -Wl,-Bdynamic")
    
    # For OpenSSL static linking, prefer static libraries
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    
    message(STATUS "  - MinGW runtime: static")
    message(STATUS "  - OpenSSL: prefer static")
endif()

# MSVC: Always prefer static OpenSSL from vcpkg
if (MSVC)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    message(STATUS "MSVC: Preferring static OpenSSL")
endif()

# Include CMake helper functions
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/func.cmake)

# Include dependency checking module
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DependencyCheck.cmake)

# Run dependency checks (provides hints for missing dependencies)
run_dependency_checks()

# ========================================
# Qt Detection (for Qt-based applications)
# ========================================
if (BUILD_QT_APPS)
    set(QT_FOUND_PATH "")
    
    if (NOT DEFINED QT_INSTALL_PATH OR QT_INSTALL_PATH STREQUAL "")
        if (APPLE)
            set(QT_POSSIBLE_PATHS
                "$ENV{HOME}/Qt/6.9.3/macos"
                "$ENV{HOME}/Qt/6.5.3/macos"
                "$ENV{HOME}/Qt/6.5.0/macos"
                "$ENV{QTDIR}"
            )
        elseif (UNIX AND NOT APPLE)
            set(QT_POSSIBLE_PATHS
                "$ENV{HOME}/Qt/6.9.3/gcc_64"
                "$ENV{HOME}/Qt/6.5.3/gcc_64"
                "$ENV{HOME}/Qt/6.5.0/gcc_64"
                "$ENV{QTDIR}"
            )
        elseif (WIN32)
            # Detect compiler type and search for matching Qt builds
            if(MINGW)
                message(STATUS "Detected MinGW compiler - searching for MinGW Qt builds")
                set(QT_POSSIBLE_PATHS
                    "C:/Qt/6.9.3/mingw_64"
                    "C:/Qt/6.5.3/mingw_64"
                    "C:/Qt/6.5.0/mingw_64"
                    "$ENV{QTDIR}"
                )
            elseif(MSVC)
                message(STATUS "Detected MSVC compiler - searching for MSVC Qt builds")
                set(QT_POSSIBLE_PATHS
                    "C:/Qt/6.9.3/msvc2022_64"
                    "C:/Qt/6.9.3/msvc2019_64"
                    "C:/Qt/6.5.3/msvc2019_64"
                    "C:/Qt/6.5.0/msvc2019_64"
                    "$ENV{QTDIR}"
                )
            else()
                message(FATAL_ERROR "Unknown Windows compiler. Only MinGW and MSVC are supported. CMAKE_CXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}")
            endif()
        endif ()
        
        foreach(POSSIBLE_PATH ${QT_POSSIBLE_PATHS})
            if(EXISTS "${POSSIBLE_PATH}/lib/cmake/Qt6/Qt6Config.cmake")
                set(QT_FOUND_PATH "${POSSIBLE_PATH}")
                message(STATUS "Found Qt6 at: ${QT_FOUND_PATH}")
                break()
            endif()
        endforeach()
    else ()
        foreach(POSSIBLE_PATH ${QT_INSTALL_PATH})
            string(STRIP "${POSSIBLE_PATH}" POSSIBLE_PATH)
            if(EXISTS "${POSSIBLE_PATH}/lib/cmake/Qt6/Qt6Config.cmake")
                set(QT_FOUND_PATH "${POSSIBLE_PATH}")
                message(STATUS "Found Qt6 at user-specified path: ${QT_FOUND_PATH}")
                break()
            endif()
        endforeach()
    endif ()
    
    if(QT_FOUND_PATH)
        list(APPEND CMAKE_PREFIX_PATH "${QT_FOUND_PATH}")
        set(QT_INSTALL_PATH "${QT_FOUND_PATH}" CACHE PATH "Path to Qt installation")
        message(STATUS "Qt6 configured at: ${QT_FOUND_PATH}")
    else()
        message(WARNING "Qt6 not found. Qt-based applications will not be built.")
        message(WARNING "Set -DQT_INSTALL_PATH=/path/to/Qt6 or install Qt6.")
        set(BUILD_QT_APPS OFF)
    endif()
endif ()

# ========================================
# Core Library
# ========================================
message(STATUS ">>> Building core library")
add_subdirectory(core/src)

# ========================================
# Automation Library (if enabled)
# ========================================
if (ENABLE_AUTOMATION)
    message(STATUS ">>> Building automation library")
    add_subdirectory(core/automation)
endif ()

# ========================================
# Emulator Test Console Client
# ========================================
message(STATUS ">>> Building testclient")
add_subdirectory(testclient)

# ========================================
# Qt-based Applications
# ========================================
if (BUILD_QT_APPS)
    message(STATUS ">>> Building unreal-qt")
    add_subdirectory(unreal-qt)
    
    message(STATUS ">>> Building unreal-screen-viewer")
    add_subdirectory(unreal-screen-viewer)
    
    message(STATUS ">>> Building unreal-videowall")
    add_subdirectory(unreal-videowall)
endif ()

# ========================================
# Unit Tests
# ========================================
if (TESTS)
	message(STATUS ">>> Building tests")
	add_subdirectory(core/tests)
endif (TESTS)

# ========================================
# Benchmarks
# ========================================
if (BENCHMARKS)
	message(STATUS ">>> Building benchmarks")
	# Prevent Google benchmark from building its own tests
	set(BENCHMARK_ENABLE_TESTING OFF)
	add_subdirectory(core/benchmarks)
endif (BENCHMARKS)

# ========================================
# Summary
# ========================================
message(STATUS "")
message(STATUS "========== Unreal-NG Build Configuration ==========")
message(STATUS "  BUILD_QT_APPS:              ${BUILD_QT_APPS}")
message(STATUS "  TESTS:                      ${TESTS}")
message(STATUS "  BENCHMARKS:                 ${BENCHMARKS}")
message(STATUS "  ENABLE_AUTOMATION:          ${ENABLE_AUTOMATION}")
if (ENABLE_AUTOMATION)
    message(STATUS "    ENABLE_LUA_AUTOMATION:    ${ENABLE_LUA_AUTOMATION}")
    message(STATUS "    ENABLE_PYTHON_AUTOMATION: ${ENABLE_PYTHON_AUTOMATION}")
    message(STATUS "    ENABLE_WEBAPI_AUTOMATION: ${ENABLE_WEBAPI_AUTOMATION}")
    message(STATUS "    ENABLE_CLI_AUTOMATION:    ${ENABLE_CLI_AUTOMATION}")
endif ()
message(STATUS "====================================================")

# ========================================
# Combined Packaging (All Qt Apps)
# ========================================
if (BUILD_QT_APPS)
    # Shared resource paths
    set(DATA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/data")
    set(CONFIG_FILE "${DATA_PATH}/configs/pentagon512k/unreal.ini")
    set(SUITE_STAGING_DIR "${CMAKE_BINARY_DIR}/packages/UnrealNG-Suite")
    
    # macOS: Combined DMG with all apps
    if(APPLE)
        # Find macdeployqt in the Qt installation
        find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
            HINTS "${QT_INSTALL_PATH}/bin" "${QT_FOUND_PATH}/bin"
            DOC "Path to macdeployqt executable"
        )
        
        add_custom_target(package_suite_macos
            # Create staging directory
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${SUITE_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SUITE_STAGING_DIR}"
            
            # Copy all three app bundles using cp -a to PRESERVE symlinks (critical for Qt frameworks)
            COMMAND cp -a "${CMAKE_BINARY_DIR}/bin/unreal-qt.app" "${SUITE_STAGING_DIR}/UnrealNG.app"
            COMMAND cp -a "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer.app" "${SUITE_STAGING_DIR}/Screen Viewer.app"
            COMMAND cp -a "${CMAKE_BINARY_DIR}/bin/unreal-videowall.app" "${SUITE_STAGING_DIR}/Video Wall.app"
            
            # Copy shared resources into the MAIN bundle BEFORE macdeployqt runs
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/fonts" "${SUITE_STAGING_DIR}/UnrealNG.app/Contents/Resources/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/rom" "${SUITE_STAGING_DIR}/UnrealNG.app/Contents/Resources/rom"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/testrom" "${SUITE_STAGING_DIR}/UnrealNG.app/Contents/Resources/rom"
            COMMAND ${CMAKE_COMMAND} -E copy "${CONFIG_FILE}" "${SUITE_STAGING_DIR}/UnrealNG.app/Contents/Resources/unreal.ini"
            
            # Run macdeployqt on each app bundle with ad-hoc signing as the FINAL step
            COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${SUITE_STAGING_DIR}/UnrealNG.app" -always-overwrite -codesign=-
            COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${SUITE_STAGING_DIR}/Screen Viewer.app" -always-overwrite -codesign=-
            COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${SUITE_STAGING_DIR}/Video Wall.app" -always-overwrite -codesign=-
            
            # Create combined DMG from the correctly signed staging directory
            COMMAND hdiutil create -volname "UnrealNG Suite" -srcfolder "${SUITE_STAGING_DIR}" -ov -format UDZO "${CMAKE_BINARY_DIR}/packages/UnrealNG-Suite.dmg"
            
            DEPENDS unreal-qt unreal-screen-viewer unreal-videowall
            COMMENT "Creating combined macOS DMG with all applications"
        )
    endif()
    
    # Windows: Combined ZIP with all apps, Qt dependencies, and shared resources
    if(WIN32)
        # Find windeployqt6 in the Qt installation
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt6
            HINTS "${QT_INSTALL_PATH}/bin" "${QT_FOUND_PATH}/bin"
            DOC "Path to windeployqt6 executable"
        )
        
        # Create base packaging target (common to all Windows toolchains)
        add_custom_target(package_suite_windows
            # Create staging directory
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${SUITE_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SUITE_STAGING_DIR}"
            
            # Copy all executables
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/bin/unreal-qt.exe"
                "${SUITE_STAGING_DIR}/"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer.exe"
                "${SUITE_STAGING_DIR}/"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/bin/unreal-videowall.exe"
                "${SUITE_STAGING_DIR}/"
            
            # Run windeployqt on each executable to copy Qt DLLs and plugins
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" "${SUITE_STAGING_DIR}/unreal-qt.exe" --no-translations
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" "${SUITE_STAGING_DIR}/unreal-screen-viewer.exe" --no-translations
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" "${SUITE_STAGING_DIR}/unreal-videowall.exe" --no-translations
            
            # Copy shared resources (only once)
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${DATA_PATH}/fonts" "${SUITE_STAGING_DIR}/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${DATA_PATH}/rom" "${SUITE_STAGING_DIR}/rom"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${DATA_PATH}/testrom" "${SUITE_STAGING_DIR}/rom"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CONFIG_FILE}" "${SUITE_STAGING_DIR}/unreal.ini"
            
            # Create ZIP archive
            COMMAND ${CMAKE_COMMAND} -E tar "cf"
                "${CMAKE_BINARY_DIR}/packages/UnrealNG-Suite-Windows.zip"
                "--format=zip" "--" "${SUITE_STAGING_DIR}"
            
            DEPENDS unreal-qt unreal-screen-viewer unreal-videowall
            COMMENT "Creating combined Windows ZIP with all applications and Qt dependencies"
        )
        
        # MinGW-specific: Add target to copy MSYS2 runtime DLLs
        if(MINGW)
            # Get MSYS2 bin path from compiler location
            get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
            message(STATUS "MinGW bin directory for packaging: ${MINGW_BIN_DIR}")
            
            add_custom_target(package_mingw_runtime
                # Copy MinGW runtime DLLs
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
                    "${SUITE_STAGING_DIR}/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libstdc++-6.dll"
                    "${SUITE_STAGING_DIR}/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libwinpthread-1.dll"
                    "${SUITE_STAGING_DIR}/"
                    
                # Copy OpenSSL DLLs (required for WebAPI/network features)
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libcrypto-3-x64.dll"
                    "${SUITE_STAGING_DIR}/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libssl-3-x64.dll"
                    "${SUITE_STAGING_DIR}/"
                    
                DEPENDS package_suite_windows
                COMMENT "Copying MinGW/MSYS2 runtime DLLs for packaging"
            )
        endif()
    endif()
    
    # Linux: Combined tarball with all apps and shared resources
    if(UNIX AND NOT APPLE)
        add_custom_target(package_suite_linux
            # Create staging directory
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${SUITE_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SUITE_STAGING_DIR}"
            
            # Copy all executables
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/bin/unreal-qt"
                "${SUITE_STAGING_DIR}/"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer"
                "${SUITE_STAGING_DIR}/"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/bin/unreal-videowall"
                "${SUITE_STAGING_DIR}/"
            
            # Copy shared resources (only once)
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${DATA_PATH}/fonts" "${SUITE_STAGING_DIR}/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${DATA_PATH}/rom" "${SUITE_STAGING_DIR}/rom"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${DATA_PATH}/testrom" "${SUITE_STAGING_DIR}/rom"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CONFIG_FILE}" "${SUITE_STAGING_DIR}/unreal.ini"
            
            # Create tarball
            COMMAND ${CMAKE_COMMAND} -E tar "cfz"
                "${CMAKE_BINARY_DIR}/packages/UnrealNG-Suite-Linux.tar.gz"
                "--" "${SUITE_STAGING_DIR}"
            
            DEPENDS unreal-qt unreal-screen-viewer unreal-videowall
            COMMENT "Creating combined Linux tarball with all applications"
        )
    endif()
endif()