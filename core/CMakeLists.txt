# core/ — GL foundation (SPEC §3). This is the SOLE owner of raw GL calls;
# render/, app/, tests/ use core/ wrappers. io/, data/, volume/ are GL-free.

add_library(re_core STATIC
    load_core_gl.cpp
    re_context.cpp
    window.cpp
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
    read_pixels.cpp
)

target_include_directories(re_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
)

target_link_libraries(re_core PUBLIC
    re_data
    spdlog::spdlog
    glm::glm
    re_project_warnings
)

# GL loader + windowing are private to core/ — no downstream header needs
# <glad/gl.h> or <GLFW/glfw3.h> after the T5 firewall (re_context.hpp is
# GL-call-free). Core's .cpp files still need them privately, and downstream
# that directly includes GLFW/glad (utils/offscreen_context.cpp,
# tests that probe GLFW) links them explicitly in their own CMakeLists.
target_link_libraries(re_core PRIVATE
    glfw
    glad_gl_core_46
)

# EGL is used by the surfaceless fallback in utils/offscreen_context.cpp (the
# offscreen context moved out of core/ in V2.1); core/ itself only needs GLFW.
find_package(Threads REQUIRED)
target_link_libraries(re_core PUBLIC Threads::Threads)

# Raw GL / EGL loader symbols live only under core/.
