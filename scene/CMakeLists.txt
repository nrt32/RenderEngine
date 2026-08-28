# scene/ — app-side scene value library (SPEC §3.1, V3.1) — GL/RE-free.
# Links only to data/volume/glm: the layer disposition requires that scene/
# never depends on render/ or GL (broker/ is the only library that may see
# both sides), so the app-side scene stays portable and RE-agnostic.
add_library(re_scene STATIC
    camera.cpp
    store.cpp
    layout.cpp
)
target_link_libraries(re_scene PUBLIC re_data re_volume)
target_link_libraries(re_scene PUBLIC $<BUILD_INTERFACE:glm::glm>)
target_include_directories(re_scene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)
# Headers are interface; ensure header-only types are visible via INTERFACE.
target_link_libraries(re_scene PUBLIC re_project_warnings re_project_sanitizers)
