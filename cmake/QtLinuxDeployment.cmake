# Run in each GUI target's directory, where its imported Qt modules and
# plugins are visible. Native package binaries/resources are installed by
# LinuxPackaging.cmake; these scripts add the private Qt runtime.
if(UNREAL_BUNDLE_QT AND UNIX AND NOT APPLE)
    # Qt deploys the desktop platform plugin by default; CI also needs offscreen.
    qt_import_plugins(${PROJECT_NAME} INCLUDE Qt6::QOffscreenIntegrationPlugin)
    qt_generate_deploy_script(TARGET ${PROJECT_NAME} OUTPUT_SCRIPT _qt_deploy_script
        CONTENT "
set(QT_DEPLOY_BIN_DIR lib/unreal-ng)
set(QT_DEPLOY_LIB_DIR lib/unreal-ng/lib)
set(QT_DEPLOY_PLUGINS_DIR lib/unreal-ng/plugins)
qt_deploy_runtime_dependencies(
    EXECUTABLE \"$<TARGET_FILE:${PROJECT_NAME}>\"
    GENERATE_QT_CONF
    NO_TRANSLATIONS
)
")
    qt_finalize_target(${PROJECT_NAME})
    install(SCRIPT "${_qt_deploy_script}" COMPONENT Suite)
endif()
