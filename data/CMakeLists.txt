# data/ — CPU containers, no GL (SPEC S3). Mesh + Image containers (T4).

add_library(re_data STATIC
    mesh.cpp
    image.cpp
)

target_include_directories(re_data PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)

# glm is a pure-math header-only dependency (no GL calls), so data/ stays
# GL-free. re_project_warnings keeps data/ sources clean under -Werror.
target_link_libraries(re_data PUBLIC
    glm::glm
    re_project_warnings
)
