# render/ — ONE class per rendering technique (SPEC §3). Uses core/ GL wrappers
# only (guardrail gpu_api_ownership). T7 adds IMaterial + PhongMaterial,
# ITransparencyPipeline (interface), MeshGeometry, and the MeshRenderer opaque
# forward pass.

add_library(re_render STATIC
    asset_registry.cpp
    phong_material.cpp
    mesh_geometry.cpp
    mesh_renderer.cpp
    linked_list_oit.cpp
    plane_renderer.cpp
    volume_renderer.cpp
    volume_slice_renderer.cpp
    slice_renderer.cpp
    contour_renderer.cpp
    view_target.cpp
    view.cpp
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

# Shaders live as .glsl files under render/shaders/ (SPEC §9 V2.6). The
# absolute source path is baked at compile time so the renderers can load
# them via core::ShaderProgram's file helpers regardless of cwd.
target_compile_definitions(re_render PRIVATE
    RE_SHADER_DIR="${CMAKE_SOURCE_DIR}/render/shaders"
)
