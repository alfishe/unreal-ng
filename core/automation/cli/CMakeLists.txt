# CLI Automation Module
project(automation-cli LANGUAGES CXX)

get_filename_component(CORE_LIBRARY_PATH
    "${CMAKE_CURRENT_LIST_DIR}/../../../core"
    ABSOLUTE
)


# Add source files
set(SOURCES
    src/automation-cli.cpp
    src/cli-processor.cpp
)

# Create the library
add_library(${PROJECT_NAME} STATIC ${SOURCES})


# Set include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CORE_LIBRARY_PATH}/src  # For emulator headers
)

# Install the library
install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include/automation/cli
    FILES_MATCHING PATTERN "*.h"
)

# Make sure the library is built with the same C++ standard
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Link against Winsock on Windows
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif ()

# Install the library
install(TARGETS automation-cli
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include/automation/cli
    FILES_MATCHING PATTERN "*.h"
)


# <Diagnostics>
message(STATUS "<${PROJECT_NAME} diagnostics>")
message(STATUS "  PROJECT_NAME: ${PROJECT_NAME}")
message(STATUS "  CORE_LIBRARY_PATH: ${CORE_LIBRARY_PATH}")
message(STATUS "  INCLUDE_DIRECTORIES:")
get_property(include_dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach (dir ${include_dirs})
    message(STATUS "                       ${dir}")
endforeach ()

message(STATUS "</${PROJECT_NAME} diagnostics>")
# </Diagnostics>