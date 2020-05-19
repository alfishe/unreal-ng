# Function:                 EXCLUDE_FILES_FROM_DIR_IN_LIST
# Description:              Exclude all files from a list under a specific directory.
# Param _InFileList:        Input and returned List 
# Param _excludeDirName:    Name of the directory, which shall be ignored.
# Param _verbose:           Print the names of the files handled
# Param outVar:             Filtered file list as result
function (EXCLUDE_FILES_FROM_DIR_IN_LIST _InFileList _excludeDirName _verbose OUTVAR outVar)
  #message (STATUS "InFileList:${_InFileList}")

  foreach (ITR ${_InFileList})
    if ("${_verbose}")
      message(STATUS "ITR=${ITR}")
    endif ("${_verbose}")

    if ("${ITR}" MATCHES "(.*)${_excludeDirName}(.*)")                   # Check if the item matches the directory name in _excludeDirName
      message(STATUS "Remove Item from List:${ITR}")
      list (REMOVE_ITEM _InFileList ${ITR})                              # Remove the item from the list
    endif ("${ITR}" MATCHES "(.*)${_excludeDirName}(.*)")
  endforeach(ITR)

  #message (STATUS "Result:${_InFileList}")

  set(${outVar} ${_InFileList} PARENT_SCOPE)                          # Return the SOURCE_FILES variable to the calling parent
endfunction (EXCLUDE_FILES_FROM_DIR_IN_LIST)

# Function:                 GET_OSX_VERSION
# Description:              Gets macOS major version (i.e. 14 eq. Mojave, 15 eq. Catalina)
# Param outVar:             Integer number representing macOS major version
function (GET_OSX_VERSION OUTVAR outVar)
  execute_process(COMMAND sw_vers OUTPUT_VARIABLE OSX_VERSION)
  string(REGEX MATCH "([0-9]+.[0-9]+)" OSX_VERSION ${OSX_VERSION})
  message("OSX_VERSION: ${OSX_VERSION}")
  string(REGEX MATCH "[0-9]+$" OSX_FAMILY ${OSX_VERSION})

  set(${outVar} ${OSX_VERSION} PARENT_SCOPE)
endfunction(GET_OSX_VERSION)