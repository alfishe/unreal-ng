# installs OpenSSL CMAKE_INSTALL_PREFIX/lib64/lib{crypto,ssl}.{a,so}
include(ExternalProject)

if(find)
  if(APPLE AND NOT OPENSSL_ROOT_DIR)
    # MacOS needs hints to find OpenSSL from Homebrew
    find_program(BREW NAMES brew)
    if(BREW)
      execute_process(COMMAND ${BREW} --prefix openssl@1.1
      RESULT_VARIABLE ret
      OUTPUT_VARIABLE out
      OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if(ret EQUAL 0)
        if(IS_DIRECTORY ${out})
          set(OPENSSL_ROOT_DIR ${out})
        endif()
      endif()
    endif()
  endif()

  find_package(OpenSSL)
endif(find)

if(OPENSSL_FOUND)
  add_custom_target(ssl)
  return()
endif()

string(JSON ssl_url GET ${json_meta} ssl url)
string(JSON ssl_tag GET ${json_meta} ssl tag)

set(ssl_config_args
--openssldir=${CMAKE_INSTALL_PREFIX}
--prefix=${CMAKE_INSTALL_PREFIX}
CC=${CC}
)
if(BUILD_SHARED_LIBS)
  list(APPEND ssl_config_args shared)
endif()
# bad options?
# --no-ssl2
# --no-weak-ssl-ciphers

ExternalProject_Add(ssl
GIT_REPOSITORY ${ssl_url}
GIT_TAG ${ssl_tag}
GIT_SHALLOW true
CONFIGURE_COMMAND <SOURCE_DIR>/config ${ssl_config_args}
BUILD_COMMAND ${MAKE_EXECUTABLE} -j
INSTALL_COMMAND ${MAKE_EXECUTABLE} -j install_sw
TEST_COMMAND ""
CONFIGURE_HANDLED_BY_BUILD ON
INACTIVITY_TIMEOUT 60
)
