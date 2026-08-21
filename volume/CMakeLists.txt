# volume/ — pure math, no GL (SPEC S3). Populated in later tasks (T6).

add_library(re_volume INTERFACE)
target_include_directories(re_volume INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
