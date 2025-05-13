# installs CMAKE_INSTALL_PREFIX/bin/libtool[ize]
include(ExternalProject)

if(LIBTOOL_EXECUTABLE)
  add_custom_target(libtool)
  return()
endif()

string(JSON libtool_url GET ${json_meta} libtool url)
string(JSON libtool_tag GET ${json_meta} libtool tag)

set(libtool_args)


extproj_autotools(libtool ${libtool_url} ${libtool_tag} "${libtool_args}")
