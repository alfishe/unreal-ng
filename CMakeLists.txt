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

# NOTE: Python dependency checks (including MSYS2 requirements for MinGW)
# are handled by DependencyCheck.cmake via run_dependency_checks()

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

# ========================================
# Debug Symbols for Release Builds
# ========================================
# Generate debug symbols even in Release mode for crash analysis.
# Symbols are packaged separately and don't bloat release archives.
option(RELEASE_WITH_DEBUG_SYMBOLS "Generate debug symbols in Release builds" ON)

if (RELEASE_WITH_DEBUG_SYMBOLS AND CMAKE_BUILD_TYPE STREQUAL "Release")
    if (MSVC)
        # MSVC: Generate PDB files for Release builds
        # /Zi: Generate complete debug info in PDB
        # /DEBUG:FULL: Include all debug info in PDB (not /DEBUG:FASTLINK)
        # /OPT:REF,ICF: Still optimize (remove unused functions, fold identical code)
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG:FULL /OPT:REF /OPT:ICF")
        set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG:FULL /OPT:REF /OPT:ICF")
        message(STATUS "MSVC Release with PDB symbols: ENABLED")
    elseif (MINGW)
        # MinGW: Generate DWARF debug info, will be separated later
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -g")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g")
        message(STATUS "MinGW Release with DWARF symbols: ENABLED")
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # GCC/Clang on Linux/macOS: Generate debug info
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -g")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g")
        message(STATUS "GCC/Clang Release with debug symbols: ENABLED")
    endif()
endif()

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

# ========================================
# MinGW Python Configuration (Early Discovery)
# ========================================
# We discover Python early for MinGW to ensure all components (including pybind11)
# use the same MSYS2-provided version and avoid picking up system Python.
if(MINGW AND ENABLE_PYTHON_AUTOMATION)
    message(STATUS "[Root] MinGW: Pre-configuring MSYS2 Python")

    # Don't search Windows registry, let CMake find MSYS2 Python from PATH
    set(Python3_FIND_REGISTRY NEVER)
    set(Python3_FIND_STRATEGY LOCATION)

    find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

    # Get the bin directory from the Python executable path
    get_filename_component(PYTHON3_BIN_DIR "${Python3_EXECUTABLE}" DIRECTORY)
    set(PYTHON3_BIN_DIR "${PYTHON3_BIN_DIR}" CACHE PATH "Python bin directory" FORCE)

    # Extract version for DLL naming (e.g., 3.12 -> 312)
    string(REPLACE "." "" PYTHON3_VERSION_NODOT "${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}")
    set(PYTHON3_VERSION_NODOT "${PYTHON3_VERSION_NODOT}" CACHE STRING "Python version without dots" FORCE)

    message(STATUS "[Root] Found MinGW Python: ${Python3_EXECUTABLE} (${Python3_VERSION})")
    message(STATUS "[Root] Python bin dir: ${PYTHON3_BIN_DIR}")
    message(STATUS "[Root] Python version for DLL: ${PYTHON3_VERSION_NODOT}")
    
    # MinGW Debug ABI Fix: MSYS2 only provides release Python libraries.
    # In Debug builds (_DEBUG defined), Python headers enable debug reference counting
    # flags (Py_REF_DEBUG) which requires symbols like _Py_NegativeRefcount that 
    # are only in debug builds of Python. 
    # We define Py_NO_ENABLE_SHARED to avoid SOME issues, but a global define
    # to disable debug refcounting is more reliable for dynamic linking.
    add_compile_definitions($<$<CONFIG:Debug>:Py_REF_DEBUG_DISABLED>)
    
    # We also define the missing symbols as empty stubs in automation_python later
    # if they are still needed.
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
        # Check environment variable first (set by Docker container)
        if(DEFINED ENV{QT_INSTALL_PATH} AND NOT "$ENV{QT_INSTALL_PATH}" STREQUAL "")
            set(QT_INSTALL_PATH "$ENV{QT_INSTALL_PATH}" CACHE PATH "Qt install path from environment")
            message(STATUS "QT_INSTALL_PATH from environment: $ENV{QT_INSTALL_PATH}")
        endif()
    endif()

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
            
            # Copy shared resources into ALL app bundles
            # (unreal-qt POST_BUILD already copies to bin/unreal-qt.app, but cp -a may miss if POST_BUILD ran before copy)
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/fonts" "${SUITE_STAGING_DIR}/UnrealNG.app/Contents/Resources/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/rom" "${SUITE_STAGING_DIR}/UnrealNG.app/Contents/Resources/rom"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/testrom" "${SUITE_STAGING_DIR}/UnrealNG.app/Contents/Resources/rom"
            COMMAND ${CMAKE_COMMAND} -E copy "${CONFIG_FILE}" "${SUITE_STAGING_DIR}/UnrealNG.app/Contents/Resources/unreal.ini"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/fonts" "${SUITE_STAGING_DIR}/Screen Viewer.app/Contents/Resources/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/rom" "${SUITE_STAGING_DIR}/Screen Viewer.app/Contents/Resources/rom"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/testrom" "${SUITE_STAGING_DIR}/Screen Viewer.app/Contents/Resources/rom"
            COMMAND ${CMAKE_COMMAND} -E copy "${CONFIG_FILE}" "${SUITE_STAGING_DIR}/Screen Viewer.app/Contents/Resources/unreal.ini"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/fonts" "${SUITE_STAGING_DIR}/Video Wall.app/Contents/Resources/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/rom" "${SUITE_STAGING_DIR}/Video Wall.app/Contents/Resources/rom"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${DATA_PATH}/testrom" "${SUITE_STAGING_DIR}/Video Wall.app/Contents/Resources/rom"
            COMMAND ${CMAKE_COMMAND} -E copy "${CONFIG_FILE}" "${SUITE_STAGING_DIR}/Video Wall.app/Contents/Resources/unreal.ini"
            
            # Copy shared icon to bundles that reference it
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/unreal-qt/install/macos/unreal.icns" "${SUITE_STAGING_DIR}/UnrealNG.app/Contents/Resources/unreal.icns"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/unreal-qt/install/macos/unreal.icns" "${SUITE_STAGING_DIR}/Screen Viewer.app/Contents/Resources/unreal.icns"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/unreal-qt/install/macos/unreal.icns" "${SUITE_STAGING_DIR}/Video Wall.app/Contents/Resources/unreal.icns"
            
            # Run macdeployqt to deploy Qt frameworks into each bundle
            COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${SUITE_STAGING_DIR}/UnrealNG.app" -always-overwrite
            COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${SUITE_STAGING_DIR}/Screen Viewer.app" -always-overwrite
            COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${SUITE_STAGING_DIR}/Video Wall.app" -always-overwrite
            
            # Sign each app with hardened runtime and entitlements
            # macdeployqt doesn't support entitlements, so we sign manually after deployment
            # -f forces re-signing of already-signed frameworks
            # --options runtime enables hardened runtime (required by Gatekeeper)
            # --entitlements injects the JIT/shared-memory permissions the emulator needs
            COMMAND codesign --force --deep --options runtime --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/install/macos/entitlements.plist" --sign - "${SUITE_STAGING_DIR}/UnrealNG.app"
            COMMAND codesign --force --deep --options runtime --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/install/macos/entitlements.plist" --sign - "${SUITE_STAGING_DIR}/Screen Viewer.app"
            COMMAND codesign --force --deep --options runtime --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/install/macos/entitlements.plist" --sign - "${SUITE_STAGING_DIR}/Video Wall.app"
            
            # Create combined DMG from the correctly signed staging directory
            COMMAND hdiutil create -volname "UnrealNG Suite" -srcfolder "${SUITE_STAGING_DIR}" -ov -format UDZO "${CMAKE_BINARY_DIR}/packages/UnrealNG-Suite.dmg"
            
            DEPENDS unreal-qt unreal-screen-viewer unreal-videowall
            COMMENT "Creating combined macOS DMG with all applications (signed with entitlements)"
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
            
            # Create ZIP archive (change to parent dir to avoid nested paths)
            COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/packages"
                ${CMAKE_COMMAND} -E tar "cf"
                "UnrealNG-Suite-Windows.zip"
                "--format=zip" "--" "UnrealNG-Suite"

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
                # Re-create ZIP with MinGW runtime DLLs included
                COMMAND ${CMAKE_COMMAND} -E remove
                    "${CMAKE_BINARY_DIR}/packages/UnrealNG-Suite-Windows.zip"
                COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/packages"
                    ${CMAKE_COMMAND} -E tar "cf"
                    "UnrealNG-Suite-Windows.zip"
                    "--format=zip" "--" "UnrealNG-Suite"

                DEPENDS package_suite_windows
                COMMENT "Copying MinGW/MSYS2 runtime DLLs and re-packaging ZIP"
            )

            # Add Python DLLs copying only when Python automation is enabled
            if(ENABLE_PYTHON_AUTOMATION)
                add_custom_target(package_mingw_python
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${PYTHON3_BIN_DIR}/libpython3.dll"
                        "${SUITE_STAGING_DIR}/"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${PYTHON3_BIN_DIR}/libpython${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}.dll"
                        "${SUITE_STAGING_DIR}/"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${PYTHON3_BIN_DIR}/libpython${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}.dll"
                        "${SUITE_STAGING_DIR}/python${PYTHON3_VERSION_NODOT}.dll"
                    DEPENDS package_mingw_runtime
                    COMMENT "Copying Python DLLs for MinGW package"
                )
            endif()
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

    # ========================================
    # Debug Symbol Packaging (separate from release archives)
    # ========================================
    # These targets create symbol archives for crash analysis.
    # Symbols are NOT included in release archives to keep them small.
    # Enable with -DPACKAGE_SYMBOLS=ON (used for rolling snapshot builds).

    option(PACKAGE_SYMBOLS "Extract and package debug symbols separately" OFF)

    set(SYMBOLS_STAGING_DIR "${CMAKE_BINARY_DIR}/packages/symbols")

    if(PACKAGE_SYMBOLS AND WIN32 AND MSVC)
        # MSVC: Package PDB files
        add_custom_target(package_symbols_windows
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYMBOLS_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${SYMBOLS_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYMBOLS_STAGING_DIR}"

            # Copy PDB files for all executables
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/bin/unreal-qt.pdb"
                "${SYMBOLS_STAGING_DIR}/"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer.pdb"
                "${SYMBOLS_STAGING_DIR}/"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/bin/unreal-videowall.pdb"
                "${SYMBOLS_STAGING_DIR}/"

            # Create symbols ZIP
            COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/packages"
                ${CMAKE_COMMAND} -E tar "cf"
                "UnrealNG-Symbols-Windows-MSVC.zip"
                "--format=zip" "--" "symbols"

            DEPENDS unreal-qt unreal-screen-viewer unreal-videowall
            COMMENT "Packaging MSVC debug symbols (PDB files)"
        )
    endif()

    if(PACKAGE_SYMBOLS AND WIN32 AND MINGW)
        # MinGW: Extract DWARF debug info and strip executables
        # The symbols are extracted BEFORE the main package target runs
        add_custom_target(package_symbols_mingw
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYMBOLS_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${SYMBOLS_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYMBOLS_STAGING_DIR}"

            # Extract debug symbols from executables
            COMMAND objcopy --only-keep-debug
                "${CMAKE_BINARY_DIR}/bin/unreal-qt.exe"
                "${SYMBOLS_STAGING_DIR}/unreal-qt.debug"
            COMMAND objcopy --only-keep-debug
                "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer.exe"
                "${SYMBOLS_STAGING_DIR}/unreal-screen-viewer.debug"
            COMMAND objcopy --only-keep-debug
                "${CMAKE_BINARY_DIR}/bin/unreal-videowall.exe"
                "${SYMBOLS_STAGING_DIR}/unreal-videowall.debug"

            # Strip debug info from executables (in-place)
            COMMAND strip --strip-debug "${CMAKE_BINARY_DIR}/bin/unreal-qt.exe"
            COMMAND strip --strip-debug "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer.exe"
            COMMAND strip --strip-debug "${CMAKE_BINARY_DIR}/bin/unreal-videowall.exe"

            # Add debug link back to executable (for addr2line to find symbols)
            COMMAND objcopy --add-gnu-debuglink="${SYMBOLS_STAGING_DIR}/unreal-qt.debug"
                "${CMAKE_BINARY_DIR}/bin/unreal-qt.exe"
            COMMAND objcopy --add-gnu-debuglink="${SYMBOLS_STAGING_DIR}/unreal-screen-viewer.debug"
                "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer.exe"
            COMMAND objcopy --add-gnu-debuglink="${SYMBOLS_STAGING_DIR}/unreal-videowall.debug"
                "${CMAKE_BINARY_DIR}/bin/unreal-videowall.exe"

            # Create symbols ZIP
            COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/packages"
                ${CMAKE_COMMAND} -E tar "cf"
                "UnrealNG-Symbols-Windows-MinGW.zip"
                "--format=zip" "--" "symbols"

            DEPENDS unreal-qt unreal-screen-viewer unreal-videowall
            COMMENT "Extracting MinGW debug symbols (DWARF) and stripping executables"
        )
    endif()

    if(PACKAGE_SYMBOLS AND UNIX AND NOT APPLE)
        # Linux: Extract debug info and strip executables
        add_custom_target(package_symbols_linux
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYMBOLS_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${SYMBOLS_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYMBOLS_STAGING_DIR}"

            # Extract debug symbols
            COMMAND objcopy --only-keep-debug
                "${CMAKE_BINARY_DIR}/bin/unreal-qt"
                "${SYMBOLS_STAGING_DIR}/unreal-qt.debug"
            COMMAND objcopy --only-keep-debug
                "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer"
                "${SYMBOLS_STAGING_DIR}/unreal-screen-viewer.debug"
            COMMAND objcopy --only-keep-debug
                "${CMAKE_BINARY_DIR}/bin/unreal-videowall"
                "${SYMBOLS_STAGING_DIR}/unreal-videowall.debug"

            # Strip executables
            COMMAND strip --strip-debug "${CMAKE_BINARY_DIR}/bin/unreal-qt"
            COMMAND strip --strip-debug "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer"
            COMMAND strip --strip-debug "${CMAKE_BINARY_DIR}/bin/unreal-videowall"

            # Add debug link
            COMMAND objcopy --add-gnu-debuglink="${SYMBOLS_STAGING_DIR}/unreal-qt.debug"
                "${CMAKE_BINARY_DIR}/bin/unreal-qt"
            COMMAND objcopy --add-gnu-debuglink="${SYMBOLS_STAGING_DIR}/unreal-screen-viewer.debug"
                "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer"
            COMMAND objcopy --add-gnu-debuglink="${SYMBOLS_STAGING_DIR}/unreal-videowall.debug"
                "${CMAKE_BINARY_DIR}/bin/unreal-videowall"

            # Create symbols tarball
            COMMAND ${CMAKE_COMMAND} -E tar "cfz"
                "${CMAKE_BINARY_DIR}/packages/UnrealNG-Symbols-Linux.tar.gz"
                "--" "${SYMBOLS_STAGING_DIR}"

            DEPENDS unreal-qt unreal-screen-viewer unreal-videowall
            COMMENT "Extracting Linux debug symbols and stripping executables"
        )
    endif()

    if(PACKAGE_SYMBOLS AND APPLE)
        # macOS: Generate dSYM bundles
        add_custom_target(package_symbols_macos
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYMBOLS_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${SYMBOLS_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYMBOLS_STAGING_DIR}"

            # Generate dSYM bundles for each app
            COMMAND dsymutil "${CMAKE_BINARY_DIR}/bin/unreal-qt.app/Contents/MacOS/unreal-qt"
                -o "${SYMBOLS_STAGING_DIR}/unreal-qt.dSYM"
            COMMAND dsymutil "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer.app/Contents/MacOS/unreal-screen-viewer"
                -o "${SYMBOLS_STAGING_DIR}/unreal-screen-viewer.dSYM"
            COMMAND dsymutil "${CMAKE_BINARY_DIR}/bin/unreal-videowall.app/Contents/MacOS/unreal-videowall"
                -o "${SYMBOLS_STAGING_DIR}/unreal-videowall.dSYM"

            # Strip executables in app bundles
            COMMAND strip -S "${CMAKE_BINARY_DIR}/bin/unreal-qt.app/Contents/MacOS/unreal-qt"
            COMMAND strip -S "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer.app/Contents/MacOS/unreal-screen-viewer"
            COMMAND strip -S "${CMAKE_BINARY_DIR}/bin/unreal-videowall.app/Contents/MacOS/unreal-videowall"

            # Create symbols tarball (tar preserves dSYM bundle structure better than zip)
            COMMAND ${CMAKE_COMMAND} -E tar "cfz"
                "${CMAKE_BINARY_DIR}/packages/UnrealNG-Symbols-macOS.tar.gz"
                "--" "${SYMBOLS_STAGING_DIR}"

            DEPENDS unreal-qt unreal-screen-viewer unreal-videowall
            COMMENT "Generating macOS dSYM bundles and stripping executables"
        )
    endif()
endif()