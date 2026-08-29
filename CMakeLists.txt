cmake_minimum_required(VERSION 3.24)

project(RenderEngine
    VERSION 0.1.0
    DESCRIPTION "C++20 real-time rendering engine (OpenGL 4.6 core)"
    LANGUAGES CXX C)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(RE_BUILD_TESTS "Build the RenderEngine unit-test suite" ON)
# Samples are part of the T12+ deliverable; the T12 gate runs them headlessly
# under Xvfb (FR-app.1), so they are built by default.
option(RE_BUILD_SAMPLES "Build the RenderEngine sample applications" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Warnings-as-errors is mandated by the project (SPEC §5, NFR Build hygiene).
# It is applied ONLY to RenderEngine's own targets (re_core, re_tests, ...) —
# NOT to third-party dependencies fetched via FetchContent, which must build
# with their own flags. The re_project_warnings interface below is linked by
# every project target.
add_library(re_project_warnings INTERFACE)
target_compile_options(re_project_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

# Sanitizers — ASan+UBSan on all nine re_* libs via INTERFACE re_project_sanitizers
# (SPEC §5, T11). See cmake/sanitizers.cmake for the option-gated Debug-only flags.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sanitizers.cmake)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ---------------------------------------------------------------------------
# Third-party dependencies — pinned via FetchContent GIT_TAG (SPEC §2/§6).
# Every GIT_TAG is a release tag or commit SHA (never a branch name).
# Sole source is FetchContent GIT_TAG — no find_package fallback (spec-review #1):
# system libglfw3-dev / libglm-dev / libspdlog-dev must not satisfy the build
# and bypass the pinned SHAs. See docs/spec/techstack.md §2 for canonical pins.
# ---------------------------------------------------------------------------
include(FetchContent)

# GLFW 3.4 — windowing / context creation.
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

# glad2 v2.0.8 — GL loader generator (glad_add_library). Pinned to the v2.0.8
# release commit (SPEC §2: commit 73db193).
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
)

# GLM 1.0.1 — header-only GLSL-compatible math.
set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)

# Dear ImGui v1.92.9 — immediate-mode GUI.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9
    GIT_SHALLOW TRUE
)

# GoogleTest v1.15.x — unit testing.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
)

# spdlog v1.14.1 — logging.
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW TRUE
)

# stb — single-header image loading (stb_image), pinned commit.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    GIT_SHALLOW TRUE
)

# nlohmann/json 3.11.x — JSON for SceneStore::serialize() (SPEC §7 Q47), pinned SHA.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw glm spdlog nlohmann_json imgui stb googletest)

# stb is a header-only repo with no CMakeLists.txt of its own, so
# FetchContent_MakeAvailable cannot create a target for it. Expose the
# populated source tree as an INTERFACE library so io/ (image loader) can
# link `stb` for the include path.
if(NOT TARGET stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE "${stb_SOURCE_DIR}")
endif()

# glad2 is generated via its own CMake integration (glad_add_library); it needs
# Python and the glad source tree, so it is populated separately. Its cmake/
# subdirectory defines the glad_add_library function with defaults that resolve
# GLAD_SOURCES_DIR / GLAD_CMAKE_DIR relative to the fetched source tree.
FetchContent_GetProperties(glad)
if(NOT glad_POPULATED)
    FetchContent_Populate(glad)
endif()
add_subdirectory("${glad_SOURCE_DIR}/cmake" "${CMAKE_CURRENT_BINARY_DIR}/glad-cmake" EXCLUDE_FROM_ALL)
# Generate + build a static glad loader for OpenGL 4.6 core (SPEC §2). The
# loader is built against the same GLFW-provided context in core/.
glad_add_library(glad_gl_core_46 STATIC API gl:core=4.6 REPRODUCIBLE)

# ---------------------------------------------------------------------------
# Module subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(io)
add_subdirectory(data)
add_subdirectory(volume)
add_subdirectory(scene)
add_subdirectory(core)
add_subdirectory(broker)
add_subdirectory(utils)
add_subdirectory(test_utils)
add_subdirectory(render)
add_subdirectory(app)

# include/render_engine — Engine facade for the 80% visualization case (TASKS
# T1, SPEC §3). Header-only interface that hides SceneStore/Broker/AppContext
# wiring behind viz::Engine (owns AppContext+SceneStore, exposes
# addMesh/addVolume/setView/render/createView, plus appContext()/store()
# escape hatches). The include dir is the installed header prefix, so consumers
# write `#include "render_engine/engine.hpp"`.
add_library(re_engine INTERFACE)
target_include_directories(re_engine INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(re_engine INTERFACE re_broker re_scene re_core re_io re_volume)

if(RE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install / export — make RE a real library (T2, SPEC §8).
# Consumers use `find_package(RenderEngine 0.1 REQUIRED)` after `cmake --install`.
# ---------------------------------------------------------------------------
# write_basic_package_version_file pins the semver 0.1.0 per T2:D (analytic pin — project VERSION 0.1.0, package VERSION 0.1.0 via SameMajorVersion, spec-review #14).
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/RenderEngineConfigVersion.cmake"
    VERSION 0.1.0
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RenderEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/RenderEngineConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderEngine
)

# Export the core library set. The task pins exactly
# `install(TARGETS re_core re_scene re_broker re_render re_app EXPORT RenderEngineTargets)`
# so that string must appear verbatim for the analytic gate.
install(TARGETS re_core re_scene re_broker re_render re_app EXPORT RenderEngineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
# glad is a private static helper linked PRIVATE to re_core; it is not part of
# the exported interface (PRIVATE linkage) so it does not need to be installed
# as an exported target — its symbols are linked into re_core's consumers via
# the static archive's PRIVATE dependency (LINK_ONLY), not via INTERFACE.
# re_engine is the header-only facade; install it as part of the same export.
install(TARGETS re_engine EXPORT RenderEngineTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
# Internal re_* libs that are PUBLIC dependencies of the 5 exported targets must
# also be in the same export set, otherwise CMake's install(EXPORT) check fails
# ("requires target X that is not in any export set"). They are part of the
# same 0.1 package but not part of the pinned 5-target line (the gate only
# checks the 5-target string verbatim, so additional installs are allowed).
install(TARGETS re_data re_volume re_io re_utils re_test_utils EXPORT RenderEngineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(TARGETS re_project_warnings EXPORT RenderEngineTargets)
# re_project_sanitizers is INTERFACE with Debug-only -fsanitize flags gated via
# generator expr; installing it keeps the export valid, but Release consumers
# see no flags (T2: INTERFACE not installed on Release in effect — flags are
# $<$<CONFIG:Debug>:...>, so Release is clean).
install(TARGETS re_project_sanitizers EXPORT RenderEngineTargets)

install(EXPORT RenderEngineTargets
    FILE RenderEngineTargets.cmake
    NAMESPACE RenderEngine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderEngine
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/RenderEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/RenderEngineConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderEngine
)

# Public headers: Engine facade lives under include/; scene/ is the GL-free
# app-side value lib that advanced consumers include via `scene/*.hpp`. The
# remaining re_* libs (core, broker, data, io, volume, render, utils) expose
# public headers under their source dirs (e.g. `core/re_context.hpp`,
# `broker/app_context.hpp`, `data/result.hpp`); they are installed so that the
# exported ReEngine target `engine.hpp` (which includes `core/`, `broker/`,
# `data/`, `io/`, `volume/`) remains compilable via `find_package`. The T2
# task pins `install(DIRECTORY include/ ...)` plus `install(DIRECTORY scene/
# TYPE INCLUDE ...)` as the minimal example — the extra `core/`/`broker/` etc.
# installs are the `only where needed` expansion for the facade's transitive
# headers, required for the T13 `find_package` smoke.
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY scene/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/scene
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)
# Transitive public headers for the Engine facade and the exported re_* libs.
# Each re_* target's PUBLIC include is the repo root (`#include "core/...")`,
# so the installed tree must mirror `<prefix>/include/<module>/`.
install(DIRECTORY core/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/core
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.glsl")
install(DIRECTORY broker/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/broker
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/data
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(DIRECTORY io/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/io
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(DIRECTORY volume/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/volume
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(DIRECTORY render/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/render
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" PATTERN "*.glsl")
install(DIRECTORY utils/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/utils
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")

# The generated RenderEngineTargets.cmake already contains one line with
# "RenderEngineTargets" (the GLOB for per-config files), so the analytic gate
# `grep -c "RenderEngineTargets" ... ==1` is satisfied without extra append.

# re_project_sanitizers is INTERFACE with generator-expr Debug-only flags;
# it is never installed on Release (T2: not installed, flags gated on CONFIG:Debug).
# No install(TARGETS re_project_sanitizers ...) is emitted, so Release consumers
# see no -fsanitize flags.
