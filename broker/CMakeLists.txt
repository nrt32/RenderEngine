# broker/ — heavily abstracted per-type scene→render mediation (SPEC §11, V3.2b)
# One file per Mapper, Broker registry type_index→IMapper, ViewBridge façade.
# Only lib that may include both scene/ and render/.
add_library(re_broker STATIC
    broker.cpp
    asset_store.cpp
    camera_mapper.cpp
    material_mapper.cpp
    mesh_object_mapper.cpp
    mesh_slice_object_mapper.cpp
    contour_mapper.cpp
    plane_mapper.cpp
    light_mapper.cpp
    view_mapper.cpp
    plane_object_mapper.cpp
    teapot_object_mapper.cpp
    volume_object_mapper.cpp
    volume_slice_object_mapper.cpp
    render_stack.cpp
    slice_display.cpp
    view_synchronizer.cpp
    view_compositor.cpp
    view_bridge.cpp
    app_context.cpp
)
target_link_libraries(re_broker PUBLIC re_scene re_render re_core re_project_warnings re_project_sanitizers)
target_include_directories(re_broker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
