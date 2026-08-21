# core/ — GL foundation (SPEC S3). This is the SOLE owner of raw GL calls;
# render/, app/, tests/ use core/ wrappers. io/, data/, volume/ are GL-free.

add_library(re_core STATIC
    offscreen_context.cpp
    gl_error.cpp
    logging.cpp
    vertex_buffer.cpp
    element_buffer.cpp
    vertex_array.cpp
    storage_buffer.cpp
    texture2d.cpp
    texture3d.cpp
    framebuffer.cpp
    shader_program.cpp
    transform_feedback.cpp
    draw.cpp
    read_pixels.cpp
)

target_include_directories(re_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)

target_link_libraries(re_core PUBLIC
    re_data
    glfw
    glad_gl_core_46
    spdlog::spdlog
    glm::glm
    re_project_warnings
)

# EGL is used by the surfaceless fallback in offscreen_context.cpp.
find_package(Threads REQUIRED)
find_library(RE_EGL_LIBRARY NAMES EGL REQUIRED)
target_link_libraries(re_core PUBLIC Threads::Threads)
target_link_libraries(re_core PRIVATE ${RE_EGL_LIBRARY})

# Raw GL / EGL loader symbols live only under core/.
