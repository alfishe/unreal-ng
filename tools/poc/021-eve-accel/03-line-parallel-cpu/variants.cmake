# 03: lines of a batch drawn by a thread pool (--threads N), optional deferred catch-up
# for RAM_G writes outside the active list's read set (EVE_POC_DEFER=1).
eve_variant(threads OVERLAY_DIR ${CMAKE_CURRENT_LIST_DIR}/overlay)
