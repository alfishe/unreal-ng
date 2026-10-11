# The machine state coordinator (docs/inprogress/2026-10-09-machine-state-coordinator/architecture.md):
# its own library whose include path is its own directory only. A header from the rest of the emulator does not
# compile here - the coordinator talks to participants through ports.h alone (requirements N7). The core
# library's source glob leaves this directory out and links the library instead
file(GLOB COORDINATOR_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(core-coordinator STATIC ${COORDINATOR_SOURCES})
add_library(unrealng::core-coordinator ALIAS core-coordinator)
# Set, not appended: directory-scoped include paths of a parent project (core/tests, unreal-qt configure their
# own include_directories() before adding core) must not reach this target
set_target_properties(core-coordinator PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
