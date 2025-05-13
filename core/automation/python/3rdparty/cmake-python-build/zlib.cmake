include(ExternalProject)

if(find)
  find_package(ZLIB)

  if(ZLIB_FOUND)
    add_custom_target(zlib)
    return()
  endif()
endif()

set(zlib_cmake_args
-DZLIB_COMPAT:BOOL=on
-DZLIB_ENABLE_TESTS:BOOL=off
)

# CMAKE_POSITION_INDEPENDENT_CODE=on is needed for zlib to work with Python, even when using static libs.

string(JSON zlib_url GET ${json_meta} zlib url)
string(JSON zlib_tag GET ${json_meta} zlib tag)

extproj_cmake(zlib ${zlib_url} ${zlib_tag} "${zlib_cmake_args}" "")
