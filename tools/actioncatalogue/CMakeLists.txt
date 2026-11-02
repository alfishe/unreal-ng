# The shipped action catalogue (data/streamdeck/catalogue.json) written from the registry at build time; the file
# stays in the repository for the tools that run without a build (make_deck_scenario.py, deckflow.py) and changes
# there with the actions. A cross build keeps the file as it is (the tool cannot run on the build host).
add_executable(unreal-action-catalogue main.cpp)
target_link_libraries(unreal-action-catalogue PRIVATE unrealng::core)
set_target_properties(unreal-action-catalogue PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(unreal-action-catalogue PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
if (APPLE)
    target_link_libraries(unreal-action-catalogue PRIVATE pthread iconv)
elseif (UNIX)
    target_link_libraries(unreal-action-catalogue PRIVATE pthread)
endif ()

add_custom_target(action-catalogue ALL
    COMMAND unreal-action-catalogue "${CMAKE_SOURCE_DIR}/data/streamdeck/catalogue.json"
    COMMENT "Action catalogue: data/streamdeck/catalogue.json"
    VERBATIM)
# runtime-data copies data/streamdeck beside the executables: after the catalogue is written
if (TARGET runtime-data)
    add_dependencies(runtime-data action-catalogue)
endif ()
