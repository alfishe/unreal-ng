# POC 032 (README.md): small embedded JPEG encoders against libjpeg-turbo for the Stream Deck companion.
# Needs libsdb (configure with -DUNREAL_STREAMDECK=ON -DBUILD_POC=ON) and the system libjpeg-turbo from pkg-config:
# the baseline of the comparison, POC only. Not part of any product build; poc032-jpegbench is EXCLUDE_FROM_ALL.

find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(POC032_TURBOJPEG IMPORTED_TARGET libturbojpeg)
endif ()
if (NOT POC032_TURBOJPEG_FOUND)
    message(STATUS "POC 032: libturbojpeg not found, poc032-jpegbench skipped")
    return()
endif ()

set(POC032_3RDPARTY ${CMAKE_CURRENT_SOURCE_DIR}/cpp/3rdparty)

# The candidates of stage 1 and the S1 step of stage 2 (third-party code and its POC derivative, compiled without
# warnings like the companion's hidapi). The product encoder is sdb::jpeg (companions/streamdeck/lib/src/jpeg.cpp).
add_library(poc032-encoders STATIC EXCLUDE_FROM_ALL
    ${POC032_3RDPARTY}/jpge/jpge.cpp
    ${POC032_3RDPARTY}/toojpeg/toojpeg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/step1/toojpegstep1.cpp)
target_include_directories(poc032-encoders SYSTEM PUBLIC
    ${POC032_3RDPARTY}/jpge ${POC032_3RDPARTY}/stb ${POC032_3RDPARTY} ${CMAKE_CURRENT_SOURCE_DIR}/cpp/step1)
target_compile_features(poc032-encoders PUBLIC cxx_std_20)
if (MSVC)
    target_compile_options(poc032-encoders PRIVATE /w)
else ()
    target_compile_options(poc032-encoders PRIVATE -w)
endif ()

add_executable(poc032-jpegbench EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/cpp/jpegbench.cpp)
target_link_libraries(poc032-jpegbench PRIVATE sdb poc032-encoders PkgConfig::POC032_TURBOJPEG)
if (NOT MSVC)
    target_compile_options(poc032-jpegbench PRIVATE -Wall -Wextra -Werror)
endif ()
