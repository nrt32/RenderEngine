# app/ — compositions + samples (SPEC S3). Populated in later tasks (T12-T15).

add_library(re_app INTERFACE)
target_include_directories(re_app INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
