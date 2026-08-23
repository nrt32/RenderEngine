# scene/ — app-side scene value library (SPEC §3.1, V3.1) — GL/RE-free.
# Links only to data/volume/glm per guardrail disposition_scene.
add_library(re_scene STATIC
    camera.cpp
    store.cpp
    layout.cpp
)
target_link_libraries(re_scene PUBLIC re_data re_volume)
target_link_libraries(re_scene PUBLIC glm::glm)
target_include_directories(re_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Headers are interface; ensure header-only types are visible via INTERFACE.
target_link_libraries(re_scene PUBLIC re_project_warnings)
