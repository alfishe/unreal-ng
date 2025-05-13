# installs Expat

include(ExternalProject)

if(find)
  find_package(EXPAT)
  if(EXPAT_FOUND)
    add_custom_target(expat)
    return()
  endif()
endif()

string(JSON expat_url GET ${json_meta} expat url)
string(JSON expat_tag GET ${json_meta} expat tag)

set(expat_args
-DEXPAT_BUILD_DOCS:BOOL=false
-DEXPAT_BUILD_EXAMPLES:BOOL=false
-DEXPAT_BUILD_TESTS:BOOL=false
-DEXPAT_BUILD_TOOLS:BOOL=false
)

extproj_cmake(expat ${expat_url} ${expat_tag} "${expat_args}" "expat")
