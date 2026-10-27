# Unpacks the disk images that data/configs keeps as <name>.img.7z (a 100 MB mostly empty FAT image is a
# few hundred KB packed) into the copied configs folder. Run after the configs are copied:
#   cmake -DDEST=<copied configs folder> -P cmake/ExtractConfigImages.cmake
# cmake -E tar reads 7z itself, so no 7z tool is needed. The archive is removed from DEST once unpacked;
# a stamp file next to the image remembers which archive it came from, so a rebuild that copies the same
# archive again does not unpack it again.
if(NOT DEST)
    message(FATAL_ERROR "ExtractConfigImages: -DDEST=<folder> is required")
endif()

file(GLOB_RECURSE archives "${DEST}/*.img.7z")
foreach(archive IN LISTS archives)
    get_filename_component(folder "${archive}" DIRECTORY)
    get_filename_component(archiveName "${archive}" NAME)
    string(REGEX REPLACE "\\.7z$" "" imageName "${archiveName}")
    set(image "${folder}/${imageName}")
    set(stamp "${image}.src")

    file(SHA256 "${archive}" archiveHash)
    set(stampHash "")
    if(EXISTS "${stamp}")
        file(READ "${stamp}" stampHash)
    endif()

    if(NOT EXISTS "${image}" OR NOT "${stampHash}" STREQUAL "${archiveHash}")
        message(STATUS "Unpacking ${archiveName}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${archive}"
                        WORKING_DIRECTORY "${folder}" RESULT_VARIABLE result)
        if(NOT result EQUAL 0 OR NOT EXISTS "${image}")
            message(FATAL_ERROR "ExtractConfigImages: cannot unpack ${archive}")
        endif()
        file(WRITE "${stamp}" "${archiveHash}")
    endif()
    file(REMOVE "${archive}")
endforeach()
