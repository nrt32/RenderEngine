#pragma once

// render/shader_cache.hpp — single lazy program cache for render shaders
//
// All renderers previously contained byte-identical lazy loader bodies that
// resolved the shader directory, called the file-backed ShaderProgram factory,
// checked the Result, cached the program in an optional, and returned it. This
// header consolidates that pattern into one cache type so the eight shader
// loaders share a single implementation and the gate can verify deduplication
// with a mechanical grep count.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/shader_program.hpp"
#include "data/result.hpp"

namespace re::render {

/// Lazy cache for a single shader program. Each renderer owns one or more
/// instances (for example, an opaque program and a capture program) and calls
/// the appropriate getOrLoad method once per frame; the first call compiles
/// and links the program from the .glsl files, later calls return the cached
/// handle without touching the filesystem or the GL compiler. The cache owns
/// the GL program name and frees it on destruction via the RAII wrapper.
class LazyProgramCache {
   public:
    LazyProgramCache() = default;
    LazyProgramCache(const LazyProgramCache&) = delete;
    LazyProgramCache& operator=(const LazyProgramCache&) = delete;
    LazyProgramCache(LazyProgramCache&&) noexcept = default;
    LazyProgramCache& operator=(LazyProgramCache&&) noexcept = default;

    /// Load the vertex and fragment shader files from RE_SHADER_DIR and link
    /// them into a program, caching the result. Returns a pointer to the
    /// cached program on success or a typed error carrying the driver log.
    data::Result<core::ShaderProgram*> getOrLoadFromFiles(
        const std::filesystem::path& vertPath,
        const std::filesystem::path& fragPath);

    /// Load the vertex, geometry, and fragment shader files from RE_SHADER_DIR
    /// and link them into a program. Used by geometry-shader techniques such
    /// as slice clipping and contour outlining.
    data::Result<core::ShaderProgram*> getOrLoadWithGeometryFromFiles(
        const std::filesystem::path& vertPath,
        const std::filesystem::path& geomPath,
        const std::filesystem::path& fragPath);

    /// Load the vertex, geometry, and fragment shader files and link them with
    /// transform-feedback varyings. Used by the slice cross-section capture
    /// path that reads back on-plane vertices for the geometry gate.
    data::Result<core::ShaderProgram*> getOrLoadWithTransformFeedbackFromFiles(
        const std::filesystem::path& vertPath,
        const std::filesystem::path& geomPath,
        const std::filesystem::path& fragPath,
        const std::vector<std::string>& varyings);

    /// Whether this cache already holds a compiled program.
    bool hasValue() const noexcept { return program_.has_value(); }

   private:
    std::optional<core::ShaderProgram> program_;
};

} // namespace re::render
