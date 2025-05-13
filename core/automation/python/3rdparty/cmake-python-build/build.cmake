cmake_minimum_required(VERSION 3.13)

if(NOT bindir)
  set(bindir ${CMAKE_CURRENT_LIST_DIR}/build)
endif()
get_filename_component(bindir ${bindir} ABSOLUTE)

if(NOT prefix)
  set(prefix ${bindir}/local)
endif()
get_filename_component(prefix ${prefix} ABSOLUTE)

option(find "find libraries" off)

set(conf_args -DCMAKE_INSTALL_PREFIX:PATH=${prefix} -Dfind:BOOL=${find})

execute_process(COMMAND ${CMAKE_COMMAND}
-G "Unix Makefiles"
-S${CMAKE_CURRENT_LIST_DIR}
-B${bindir}
${conf_args}
RESULT_VARIABLE ret
)
if(NOT ret EQUAL 0)
  message(FATAL_ERROR "Failed to configure")
endif()

# --- build

execute_process(COMMAND ${CMAKE_COMMAND} --build ${bindir}
RESULT_VARIABLE ret
)
if(NOT ret EQUAL 0)
  message(FATAL_ERROR "Failed to build / install")
endif()
