# volume/ — pure math, no GL (SPEC S3). T6: TransferFunction (FR-vol.1),
# ray/AABB sampling steps (FR-vol.3) and front-to-back ray-cast compositing
# (FR-vol.2). Headless-testable; consumed by render/ (VolumeRenderer) in T9.

add_library(re_volume STATIC
    transfer_function.cpp
    ray_caster.cpp
)

target_include_directories(re_volume PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)

# glm is a pure-math header-only dependency (no GL calls), so volume/ stays
# GL-free. re_project_warnings keeps volume/ sources clean under -Werror.
target_link_libraries(re_volume PUBLIC
    glm::glm
    re_project_warnings
)
