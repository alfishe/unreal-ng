function(extproj_autotools name url tag config_args)

list(PREPEND config_args
--prefix=${CMAKE_INSTALL_PREFIX}
CC=${CC}
CFLAGS=${CMAKE_C_FLAGS}
LDFLAGS=${LDFLAGS}
)

ExternalProject_Add(${name}
GIT_REPOSITORY ${url}
GIT_TAG ${tag}
GIT_SHALLOW true
CONFIGURE_COMMAND <SOURCE_DIR>/configure ${config_args}
BUILD_COMMAND ${MAKE_EXECUTABLE} -j${Ncpu}
INSTALL_COMMAND ${MAKE_EXECUTABLE} -j${Ncpu} install
TEST_COMMAND ""
CONFIGURE_HANDLED_BY_BUILD ON
INACTIVITY_TIMEOUT 60
)

endfunction()
