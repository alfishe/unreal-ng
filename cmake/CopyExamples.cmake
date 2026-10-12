# Copies data/examples to DEST (cmake -DDEST=<dir> -P CopyExamples.cmake) for the release packages.
# Leaves out what running the examples in the source tree leaves behind: Python caches, reports.
get_filename_component(_examples "${CMAKE_CURRENT_LIST_DIR}/../data/examples" ABSOLUTE)
if (NOT DEST)
    message(FATAL_ERROR "CopyExamples.cmake: DEST not set")
endif ()
file(REMOVE_RECURSE "${DEST}")
file(MAKE_DIRECTORY "${DEST}")
file(COPY "${_examples}/" DESTINATION "${DEST}"
     PATTERN "__pycache__" EXCLUDE
     PATTERN "*.pyc" EXCLUDE
     PATTERN "section-report*" EXCLUDE
     PATTERN "*-report.md" EXCLUDE
     PATTERN "*-report.json" EXCLUDE
     PATTERN ".gitignore" EXCLUDE)
