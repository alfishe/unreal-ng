# unreal-debugger: eCat3's debugger window (POC 041) over the core in its own process, or over a running unreal-qt
# through its WebAPI (--connect host:port --emulator id). tools/build/build.sh unreal-debugger
#
# Two halves that never share a translation unit: ecat's views see only the shim (shim/, its Emulator / CPU /
# Memory in namespace ecatshim), the adapter and main see unreal-ng's core, whose classes of the same names would
# clash with them.
find_package(Qt6 COMPONENTS Widgets Network REQUIRED)

set(ECAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ecat)

# eCat3 dev 27a98108 (src/dialogs/debugger, emulator/disasm_buffer.*, dialogs/numfmt.h, dialogs/genericdbgwnd.*);
# bankstrip.* and pagedmemory.* are our own (the windows of a paged space)
add_library(unreal-debugger-views STATIC
    ${ECAT_DIR}/dialogs/genericdbgwnd.cpp
    ${ECAT_DIR}/dialogs/debugger/addressmenu.cpp
    ${ECAT_DIR}/dialogs/debugger/bankstrip.cpp
    ${ECAT_DIR}/dialogs/debugger/celleditor.cpp
    ${ECAT_DIR}/dialogs/debugger/debuggerwindow.cpp
    ${ECAT_DIR}/dialogs/debugger/disasmview.cpp
    ${ECAT_DIR}/dialogs/debugger/memoryview.cpp
    ${ECAT_DIR}/dialogs/debugger/memorywindow.cpp
    ${ECAT_DIR}/dialogs/debugger/pagedmemory.cpp
    ${ECAT_DIR}/dialogs/debugger/registersview.cpp
    ${ECAT_DIR}/dialogs/debugger/sideviews.cpp
    ${ECAT_DIR}/dialogs/debugger/ttdcontrols.cpp
    ${ECAT_DIR}/emulator/disasm_buffer.cpp
)
set_target_properties(unreal-debugger-views PROPERTIES AUTOMOC ON)
# shim/emulator/*.h stand for ecat's headers of those names; shimapi/ecatshim.h is the API itself
target_include_directories(unreal-debugger-views PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shim ${CMAKE_CURRENT_SOURCE_DIR}/shimapi ${ECAT_DIR})
target_link_libraries(unreal-debugger-views PUBLIC Qt6::Widgets)

# The adapter sees the core and the API, never shim/ (its emulator/emulator.h would shadow the core's). Two
# machines behind the API: the core in this process, or a running emulator through its WebAPI (--connect)
add_library(unreal-debugger-adapter STATIC adapter/unrealmachine.cpp adapter/remotemachine.cpp)
target_include_directories(unreal-debugger-adapter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shimapi ${CMAKE_CURRENT_SOURCE_DIR}/adapter
                           PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
# PRIVATE: the core's include directories must not reach main.cpp and the views
target_link_libraries(unreal-debugger-adapter PRIVATE unrealng::core PUBLIC Qt6::Widgets Qt6::Network)
if (APPLE)
    target_link_libraries(unreal-debugger-adapter PUBLIC pthread iconv)
elseif (UNIX)
    target_link_libraries(unreal-debugger-adapter PUBLIC pthread)
endif ()

# The GUI control plane (guicontrol/): the window, the `debugger` interface and the views disassembly and memory, on
# the views' side (no core header); the machine's id and its core actions come from main through hooks
if (NOT TARGET guicontrol-widgets)
    add_subdirectory("${CMAKE_SOURCE_DIR}/guicontrol" guicontrol)
endif ()
add_library(unreal-debugger-plane STATIC guiplane/debuggercontrol.cpp)
set_target_properties(unreal-debugger-plane PROPERTIES AUTOMOC ON)
target_include_directories(unreal-debugger-plane PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/guiplane)
target_link_libraries(unreal-debugger-plane PUBLIC unreal-debugger-views guicontrol-widgets)

add_executable(unreal-debugger main.cpp resources/ecat.qrc)
set_target_properties(unreal-debugger PROPERTIES AUTOMOC ON AUTORCC ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(unreal-debugger PRIVATE unreal-debugger-plane unreal-debugger-adapter Qt6::Widgets)
add_dependencies(unreal-debugger runtime-data)
