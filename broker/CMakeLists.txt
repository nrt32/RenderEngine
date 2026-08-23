# broker/ — heavily abstracted per-type scene→render mediation (SPEC §11, V3.2b)
# One file per Mapper, Broker registry type_index→IMapper, ViewBridge façade.
# Only lib that may include both scene/ and render/.
add_library(re_broker STATIC
    broker.cpp
    asset_store.cpp
    camera_mapper.cpp
    mesh_object_mapper.cpp
    contour_mapper.cpp
    view_synchronizer.cpp
    view_compositor.cpp
    view_bridge.cpp
)
target_link_libraries(re_broker PUBLIC re_scene re_render re_core)
target_include_directories(re_broker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
