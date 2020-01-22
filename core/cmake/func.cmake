
# Function:                 EXCLUDE_FILES_FROM_DIR_IN_LIST
# Description:              Exclude all files from a list under a specific directory.
# Param _InFileList:        Input and returned List 
# Param _excludeDirName:    Name of the directory, which shall be ignored.
# Param _verbose:           Print the names of the files handled
function (EXCLUDE_FILES_FROM_DIR_IN_LIST _InFileList _excludeDirName _verbose)
  foreach (ITR ${_InFileList})
    if ("${_verbose}")
      message(STATUS "ITR=${ITR}")
    endif ("${_verbose}")

    if ("${ITR}" MATCHES "(.*)${_excludeDirName}(.*)")                   # Check if the item matches the directory name in _excludeDirName
      message(STATUS "Remove Item from List:${ITR}")
      list (REMOVE_ITEM _InFileList ${ITR})                              # Remove the item from the list
    endif ("${ITR}" MATCHES "(.*)${_excludeDirName}(.*)")

  endforeach(ITR)
  set(_InFileList ${_InFileList} PARENT_SCOPE)                          # Return the SOURCE_FILES variable to the calling parent
ENDFUNCTION (EXCLUDE_FILES_FROM_DIR_IN_LIST)


function(dump_cmake_variables)
    get_cmake_property(_variableNames VARIABLES)
    list (SORT _variableNames)
    foreach (_variableName ${_variableNames})
        if (ARGV0)
            unset(MATCHED)
            string(REGEX MATCH ${ARGV0} MATCHED ${_variableName})
            if (NOT MATCHED)
                continue()
            endif()
        endif()
        message(STATUS "${_variableName}=${${_variableName}}")
    endforeach()
endfunction()



