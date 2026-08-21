# data/ — CPU containers, no GL (SPEC S3). Header-only for v1.

add_library(re_data INTERFACE)

target_include_directories(re_data INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# data/ must stay GL-free; the interface carries no GL/GLFW/glad dependency.
