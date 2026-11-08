# The GUI control plane (docs/inprogress/2026-11-07-gui-control-plane): the host every GUI program embeds, the
# client, discovery and the interfaces (guicontrol, Qt Core + Network only, no link to the core), the window binding
# (guicontrol-widgets, Qt Widgets) and the CLI unreal-gui. Added from the root CMakeLists.txt with the Qt apps.

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/QtDiscovery.cmake)
find_package(Qt6 COMPONENTS Core Network Gui Widgets REQUIRED)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(GC_WARNINGS -Wall -Wextra -Werror -Wno-error=deprecated-declarations)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(GC_WARNINGS -Wall -Wextra -Werror)
elseif (MSVC)
    set(GC_WARNINGS /W3 /WX)
endif ()

add_library(guicontrol STATIC
    src/protocol.cpp
    src/interfaces.cpp
    src/node.cpp
    src/host.cpp
    src/discovery.cpp
    src/client.cpp
    include/guicontrol/protocol.h
    include/guicontrol/interfaces.h
    include/guicontrol/node.h
    include/guicontrol/host.h
    include/guicontrol/discovery.h
    include/guicontrol/client.h
)
target_include_directories(guicontrol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(guicontrol PUBLIC Qt6::Core Qt6::Network)
target_compile_options(guicontrol PRIVATE ${GC_WARNINGS})
set_target_properties(guicontrol PROPERTIES AUTOMOC ON)

add_library(guicontrol-widgets STATIC
    src/windowbinding.cpp
    include/guicontrol/windowbinding.h
)
target_link_libraries(guicontrol-widgets PUBLIC guicontrol Qt6::Gui Qt6::Widgets)
target_compile_options(guicontrol-widgets PRIVATE ${GC_WARNINGS})
set_target_properties(guicontrol-widgets PROPERTIES AUTOMOC ON)

add_executable(unreal-gui cli/main.cpp cli/connect.cpp cli/connect.h cli/mcp.cpp cli/mcp.h)
target_link_libraries(unreal-gui PRIVATE guicontrol)
target_compile_definitions(unreal-gui PRIVATE GUICONTROL_VERSION="${PROJECT_VERSION}")
target_compile_options(unreal-gui PRIVATE ${GC_WARNINGS})
