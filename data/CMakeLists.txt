# data/ — CPU containers, no GL (SPEC §3). Mesh + Image (T4) + VolumeDataset
# (T5) containers.

add_library(re_data STATIC
    mesh.cpp
    image.cpp
    volume_dataset.cpp
)

target_include_directories(re_data PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)

# glm is a pure-math header-only dependency (no GL calls), so data/ stays
# GL-free. re_project_warnings keeps data/ sources clean under -Werror.
# For install/export, external deps are BUILD_INTERFACE only so the export
# does not require them to be in the same export set — the Config handles
# find_dependency for consumers (T2: find_package fallbacks).
target_link_libraries(re_data PUBLIC
    $<BUILD_INTERFACE:glm::glm>
    re_project_warnings
    re_project_sanitizers
)
