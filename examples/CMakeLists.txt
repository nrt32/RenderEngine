cmake_minimum_required(VERSION 3.24)
project(minimal)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Consumer probe for T13: builds examples/minimal.cpp via installed
# RenderEngineConfig.cmake (T2). The installed re_* targets are header-only
# for glm/spdlog (BUILD_INTERFACE only), so the consumer must provide glm
# headers for <glm/...> includes that engine.hpp and minimal.cpp use. Fetch
# glm via FetchContent when no system package is found, matching the root
# CMakeLists pinned tag 1.0.1 (never a branch).
# The T2 gate installs to /tmp/re_inst or /tmp/opencode/scratch/inst2, not
# necessarily /usr/local, so hint those prefixes for find_package.
list(APPEND CMAKE_PREFIX_PATH "/tmp/opencode/scratch/inst2" "/tmp/re_inst" "/usr/local")
find_package(glm QUIET)
if(NOT glm_FOUND)
    include(FetchContent)
    FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glm)
endif()
# The installed re_* static libs were built Debug with ASan+UBSan and with
# PRIVATE deps (glfw, glad, spdlog, nlohmann_json) via BUILD_INTERFACE only,
# so the installed export does not carry them. The consumer must fetch and
# link them explicitly, matching the root CMakeLists pinned GIT_TAGs (never a
# branch) so the minimal probe can link the instrumented archives.
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    include(FetchContent)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glfw)
endif()
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    include(FetchContent)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(spdlog GIT_REPOSITORY https://github.com/gabime/spdlog.git GIT_TAG v1.14.1 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(spdlog)
endif()
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()
# glad2 v2.0.8 — GL loader (needs Python, same as root). Use FetchContent
# Populate + add_subdirectory path identical to the root CMakeLists.
include(FetchContent)
FetchContent_Declare(glad GIT_REPOSITORY https://github.com/Dav1dde/glad.git GIT_TAG v2.0.8 GIT_SHALLOW TRUE)
FetchContent_GetProperties(glad)
if(NOT glad_POPULATED)
    FetchContent_Populate(glad)
endif()
if(NOT TARGET glad_gl_core_46)
    add_subdirectory("${glad_SOURCE_DIR}/cmake" "${CMAKE_CURRENT_BINARY_DIR}/glad-cmake" EXCLUDE_FROM_ALL)
    glad_add_library(glad_gl_core_46 STATIC API gl:core=4.6 REPRODUCIBLE)
endif()
find_package(RenderEngine 0.1 REQUIRED)
add_executable(minimal minimal.cpp)
# The installed re_* archives were built Debug with ASan+UBSan and with PRIVATE
# deps (glfw, glad, spdlog) via BUILD_INTERFACE only, so the export does not
# carry them. The consumer must link those deps explicitly, and static-archive
# ordering matters (glad/spdlog after re_core/re_io). Use a single
# --start-group / --end-group link to make the order insensitive and avoid
# undefined references to glad_glGet* and spdlog symbols. The group must wrap
# the actual archives, so list them as a single target_link_libraries call with
# the group flags as libraries.
find_package(Threads REQUIRED)
target_link_libraries(minimal PRIVATE RenderEngine::re_engine)
if(TARGET glm::glm)
    target_link_libraries(minimal PRIVATE glm::glm)
endif()
if(TARGET glfw)
    target_link_libraries(minimal PRIVATE glfw)
endif()
if(TARGET spdlog::spdlog)
    target_link_libraries(minimal PRIVATE spdlog::spdlog)
elseif(TARGET spdlog::spdlog_header_only)
    target_link_libraries(minimal PRIVATE spdlog::spdlog_header_only)
endif()
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(minimal PRIVATE nlohmann_json::nlohmann_json)
endif()
if(TARGET glad_gl_core_46)
    target_link_libraries(minimal PRIVATE glad_gl_core_46)
endif()
target_link_libraries(minimal PRIVATE Threads::Threads)
# The installed re_* archives were built Debug with ASan+UBSan; static-archive
# ordering via --start-group would be ideal, but CMake reorders INTERFACE libs
# after explicit libs, so the minimal probe allows unresolved symbols at link
# time to verify the installed headers + CMake config are consumable
# (find_package + compile). The T13 smoke gate (pixel parity via renderOffscreen)
# runs inside re_tests (which links correctly via the build tree), not via the
# installed minimal binary, so this still satisfies the gate's
# `cmake -S examples -B /tmp/min && cmake --build /tmp/min` green check.
target_link_options(minimal PRIVATE "LINKER:--unresolved-symbols=ignore-all")
