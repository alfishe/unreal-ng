# Stream Deck Studio (unreal-streamdeck-studio): the software deck and scenario editor, a viewer of the daemon over
# the device wire v2 (docs/inprogress/2026-10-19-streamdeck-integration/studio.md). Built when Qt is found. Settings
# are the project's (standard, build type, LTO, deployment target from the root); warnings: SD_WARNINGS.

get_filename_component(SD_ROOT_DIR ${SD_DIR}/../.. ABSOLUTE)
if (BUILD_QT_APPS OR TARGET Qt6::Core)
    include(${SD_ROOT_DIR}/cmake/QtDiscovery.cmake)  # the same discovery as unreal-qt (requires Qt)
else ()
    find_package(Qt6 6.7 QUIET CONFIG COMPONENTS Core)
    if (NOT Qt6_FOUND)
        message(STATUS "Stream Deck Studio: Qt 6 not found, not built")
        return()
    endif ()
endif ()
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# Recording links the core's encoders (core/recording), as unreal-qt does.
if (NOT TARGET unrealng::recording)
    add_subdirectory(${SD_ROOT_DIR}/core/recording ${CMAKE_BINARY_DIR}/companions-recording)
endif ()

set(SD_STUDIO ${CMAKE_CURRENT_SOURCE_DIR})

# The deck and its links (no widgets): the window and the tests use it.
add_library(sdb-studio-deck STATIC
    ${SD_STUDIO}/virtualdeck.h ${SD_STUDIO}/virtualdeck.cpp
    ${SD_STUDIO}/decklink.h
    ${SD_STUDIO}/deckclient.h ${SD_STUDIO}/deckclient.cpp
    ${SD_STUDIO}/daemoncontrol.h ${SD_STUDIO}/daemoncontrol.cpp
    ${SD_STUDIO}/listenserver.h ${SD_STUDIO}/listenserver.cpp)
set_target_properties(sdb-studio-deck PROPERTIES AUTOMOC ON)
target_include_directories(sdb-studio-deck PUBLIC ${SD_STUDIO})
target_compile_options(sdb-studio-deck PRIVATE ${SD_WARNINGS})
target_link_libraries(sdb-studio-deck PUBLIC sdb Qt6::Core Qt6::Gui Qt6::Network)

# The deck widget and the scenario editor (widgets): the window and the editor tests use them.
add_library(sdb-studio-editor STATIC
    ${SD_STUDIO}/deckwidget.h ${SD_STUDIO}/deckwidget.cpp
    ${SD_STUDIO}/gui.h ${SD_STUDIO}/textedit.h
    ${SD_STUDIO}/editor/editordocument.h ${SD_STUDIO}/editor/editordocument.cpp
    ${SD_STUDIO}/editor/presets.h ${SD_STUDIO}/editor/presets.cpp
    ${SD_STUDIO}/editor/designcanvas.h ${SD_STUDIO}/editor/designcanvas.cpp
    ${SD_STUDIO}/editor/inspector.h ${SD_STUDIO}/editor/inspector.cpp
    ${SD_STUDIO}/editor/runcontroller.h ${SD_STUDIO}/editor/runcontroller.cpp
    ${SD_STUDIO}/editor/editorwidget.h ${SD_STUDIO}/editor/editorwidget.cpp
    ${SD_STUDIO}/editor/editorapi.h ${SD_STUDIO}/editor/editorapi.cpp)
set_target_properties(sdb-studio-editor PROPERTIES AUTOMOC ON)
target_include_directories(sdb-studio-editor PUBLIC ${SD_STUDIO} ${SD_STUDIO}/editor)
target_compile_options(sdb-studio-editor PRIVATE ${SD_WARNINGS})
target_link_libraries(sdb-studio-editor PUBLIC sdb-studio-deck Qt6::Widgets)

add_executable(unreal-streamdeck-studio
    ${SD_STUDIO}/main.cpp
    ${SD_STUDIO}/mainwindow.h ${SD_STUDIO}/mainwindow.cpp
    ${SD_STUDIO}/studioapi.h ${SD_STUDIO}/studioapi.cpp
    ${SD_STUDIO}/recorder.h ${SD_STUDIO}/recorder.cpp)
set_target_properties(unreal-streamdeck-studio PROPERTIES
    AUTOMOC ON
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Stream Deck Studio"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.unrealng.streamdeck-studio")
target_compile_options(unreal-streamdeck-studio PRIVATE ${SD_WARNINGS})
# The recording library still calls the core's ThreadHelper (its encoder threads name themselves); the core itself
# is not linked, so its one source comes along until the recording-library extraction removes the dependency.
target_sources(unreal-streamdeck-studio PRIVATE ${SD_ROOT_DIR}/core/src/common/threadhelper.cpp)
target_link_libraries(unreal-streamdeck-studio PRIVATE sdb-studio-editor unrealng::recording)
# the daemon the editor's Run starts sits beside the studio
add_dependencies(unreal-streamdeck-studio unreal-streamdeck)

# ---------------------------------------------------------------- tests (no window: offscreen)
add_executable(streamdeck-studio-tests EXCLUDE_FROM_ALL
    ${SD_STUDIO}/tests/studio_test.cpp ${SD_STUDIO}/tests/editor_test.cpp)
set_target_properties(streamdeck-studio-tests PROPERTIES AUTOMOC ON)
target_include_directories(streamdeck-studio-tests PRIVATE ${SD_DIR}/tests)
target_compile_options(streamdeck-studio-tests PRIVATE ${SD_WARNINGS})
target_link_libraries(streamdeck-studio-tests PRIVATE sdb-studio-editor)
