# DEB package with a private Qt runtime (included only with UNREAL_BUNDLE_QT=ON).
# Keeping resources alongside the real binaries preserves
# FileHelper::GetResourcesPath(); launchers live on the user's PATH.
set(UNREAL_PACKAGE_VERSION "" CACHE STRING "Automatically generated package version")
if(NOT UNREAL_PACKAGE_VERSION)
    string(TIMESTAMP UNREAL_PACKAGE_VERSION "%Y%m%d.%H%M%S" UTC)
endif()
if(NOT UNREAL_PACKAGE_VERSION MATCHES "^[0-9]+[.][0-9]+$")
    message(FATAL_ERROR "UNREAL_PACKAGE_VERSION must be YYYYMMDD.HHMMSS")
endif()

set(_suite_targets unreal-qt unreal-screen-viewer unreal-videowall)
if(TARGET unreal-mcp-bridge)
    list(APPEND _suite_targets unreal-mcp-bridge)
endif()
install(TARGETS ${_suite_targets} RUNTIME DESTINATION lib/unreal-ng COMPONENT Suite)
set_target_properties(${_suite_targets} PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
foreach(_app IN LISTS _suite_targets)
    # Relative symlinks also work in a DESTDIR staging tree.
    install(CODE "
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\")
        file(CREATE_LINK \"../lib/unreal-ng/${_app}\"
            \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/${_app}\" SYMBOLIC)
    " COMPONENT Suite)
endforeach()
install(DIRECTORY "${DATA_PATH}/fonts" "${DATA_PATH}/rom"
    "${DATA_PATH}/boot" "${DATA_PATH}/configs"
    DESTINATION lib/unreal-ng COMPONENT Suite)
install(DIRECTORY "${DATA_PATH}/testrom/" DESTINATION lib/unreal-ng/rom COMPONENT Suite)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/core/automation/webapi/resources/html"
    DESTINATION lib/unreal-ng/resources COMPONENT Suite)
install(FILES
    "${CMAKE_SOURCE_DIR}/unreal-qt/install/linux/unrealng.desktop"
    "${CMAKE_SOURCE_DIR}/unreal-videowall/install/linux/unrealng-videowall.desktop"
    DESTINATION share/applications COMPONENT Suite)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/unreal-qt/install/linux/icons/"
    "${CMAKE_SOURCE_DIR}/unreal-videowall/install/linux/icons/"
    DESTINATION share/icons COMPONENT Suite)
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION share/doc/unreal-ng COMPONENT Suite)

set(CPACK_PACKAGE_NAME "unreal-ng")
set(CPACK_PACKAGE_VENDOR "UnrealNG contributors")
set(CPACK_PACKAGE_CONTACT "UnrealNG contributors <https://github.com/alfishe/unreal-ng>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/alfishe/unreal-ng")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ZX Spectrum emulator and display utilities")
set(CPACK_PACKAGE_VERSION "${UNREAL_PACKAGE_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(UNREAL_PACKAGE_BASENAME "UnrealNG-Suite-Linux-${CMAKE_SYSTEM_PROCESSOR}"
    CACHE STRING "Native package filename without extension")
set(CPACK_PACKAGE_FILE_NAME "${UNREAL_PACKAGE_BASENAME}")
set(CPACK_STRIP_FILES ON)
# Do not ship headers/static libraries installed by vendored dependencies.
set(CPACK_COMPONENTS_ALL Suite)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
include(CPack)
