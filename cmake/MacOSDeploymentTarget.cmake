# Minimum macOS version for all Apple builds. Must be included BEFORE project().
#
# CMake defaults CMAKE_OSX_DEPLOYMENT_TARGET to the HOST OS version, so binaries
# built on the macos-14 GitHub runner refused to run on macOS 12 / 13. Qt 6.9
# supports macOS 12 and later, and the code guards newer AppKit APIs with
# @available, so 12.0 is the floor. Override with -DCMAKE_OSX_DEPLOYMENT_TARGET=...
# or the MACOSX_DEPLOYMENT_TARGET environment variable.
if(APPLE AND "${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL "" AND "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version to target" FORCE)
endif()

# Exposed to the bundles' custom Info.plist files (LSMinimumSystemVersion); CMake
# only substitutes this for its own default plist template
if(APPLE)
    set(MACOSX_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
