# render/ — ONE class per rendering technique (SPEC §3). Uses core/ GL wrappers
# only (guardrail gpu_api_ownership). T7 adds IMaterial + PhongMaterial,
# ITransparencyPipeline (interface), MeshGeometry, and the MeshRenderer opaque
# forward pass.

add_library(re_render STATIC
    phong_material.cpp
    mesh_geometry.cpp
    mesh_renderer.cpp
    linked_list_oit.cpp
    plane_renderer.cpp
    volume_renderer.cpp
    slice_renderer.cpp
    view_renderer.cpp
)

target_include_directories(re_render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)

target_link_libraries(re_render PUBLIC
    re_core
    re_data
    re_volume
    glm::glm
    spdlog::spdlog
    re_project_warnings
)
