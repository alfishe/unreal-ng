# install bzip2, needed for Python xarray/pandas

include(FetchContent)

if(find)
  find_package(BZip2)
  if(BZIP2_FOUND)
    add_custom_target(bzip2)
    return()
  endif()
endif()

string(JSON bzip2_url GET ${json_meta} bzip2 url)
string(JSON bzip2_tag GET ${json_meta} bzip2 tag)

set(bzip2_args
-DENABLE_APP:BOOL=false
-DENABLE_DOCS:BOOL=false
-DENABLE_EXAMPLES:BOOL=false
-DENABLE_TESTS:BOOL=false
-DENABLE_SHARED_LIB:BOOL=true
-DENABLE_STATIC_LIB:BOOL=false
)

# build
extproj_cmake(bzip2 ${bzip2_url} ${bzip2_tag} "${bzip2_args}" "")
