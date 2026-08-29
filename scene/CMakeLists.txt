# scene/ — app-side scene value library (SPEC §3.1, V3.1) — GL/RE-free.
# Links only to data/volume/glm: the layer disposition requires that scene/
# never depends on render/ or GL (broker/ is the only library that may see
# both sides), so the app-side scene stays portable and RE-agnostic.
add_library(re_scene STATIC
    camera.cpp
    camera_controller.cpp
    store.cpp
    layout.cpp
    view.cpp
)
target_link_libraries(re_scene PUBLIC re_data re_volume)
target_link_libraries(re_scene PUBLIC $<BUILD_INTERFACE:glm::glm>)
# T13 serialize uses nlohmann/json 3.11.3 (header-only, pinned GIT_TAG v3.11.3);
# the .cpp includes <nlohmann/json.hpp> for SceneStore::serialize()/deserialize().
# Header does not expose json types, so private BUILD_INTERFACE is sufficient —
# the installed re_scene export does not require the json package (consumers
# that parse the JSON string themselves can find_package it separately).
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(re_scene PRIVATE $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)
elseif(TARGET nlohmann_json)
    target_link_libraries(re_scene PRIVATE $<BUILD_INTERFACE:nlohmann_json>)
endif()
target_include_directories(re_scene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)
# Headers are interface; ensure header-only types are visible via INTERFACE.
target_link_libraries(re_scene PUBLIC re_project_warnings re_project_sanitizers)
