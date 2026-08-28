# io/ — loaders ONLY, no GL (SPEC §3). Mesh + image (T4) and volume NRRD (T5)
# loaders. io/ consumes data/ containers (CPU, GL-free) and stb_image for
# decoding.

add_library(re_io STATIC
    mesh/obj_mesh_loader.cpp
    image/image_loader.cpp
    volume/nrrd_volume_loader.cpp
)

target_include_directories(re_io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)

# stb is the header-only FetchContent target (include dir on the stb source
# tree). re_data provides the Mesh/Image containers and Result. Both are
# GL-free, so io/ stays GL-free; re_project_warnings keeps io/ clean under
# -Werror.
target_link_libraries(re_io PUBLIC
    re_data
    $<BUILD_INTERFACE:stb>
    $<BUILD_INTERFACE:spdlog::spdlog>
    re_project_warnings
    re_project_sanitizers
)
