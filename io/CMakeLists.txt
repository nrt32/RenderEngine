# io/ — loaders ONLY, no GL (SPEC S3). Populated in later tasks (T4/T5).

add_library(re_io INTERFACE)
target_include_directories(re_io INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
