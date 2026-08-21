# render/ — ONE class per rendering technique (SPEC S3). Populated in later
# tasks (T7-T11). Uses core/ GL wrappers only.

add_library(re_render INTERFACE)
target_include_directories(re_render INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
