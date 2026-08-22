#pragma once

// core/shader_program.hpp — RAII GLSL shader program with typed diagnostics.
//
// core/ is the SOLE owner of raw GL calls (SPEC S3, guardrail
// gpu_api_ownership); this header is GL-call-free (glm is a pure math
// dependency). The raw glCreateShader / glCompileShader / glLinkProgram calls
// live in shader_program.cpp.
//
// Diagnostics (SPEC S4 FR-core.2): compile/link failures are reported as a
// typed data::Error carrying an enumerated code (ShaderError) and a message
// built from the driver's info log. Each driver diagnostic line is normalized
// to start with the project's golden diagnostic prefix "ERROR: " followed by
// the driver's location + message, e.g.
//
//   ERROR: 0:7(27): error: `glibberish' undeclared
//
// so the offending line is unambiguously reported in "0:N" form.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "data/result.hpp"

namespace re::core {

/// Enumerated diagnostics codes carried by data::Error::code for shader
/// failures (typed, SPEC S5). Public API: lives in the header.
enum class ShaderError : int {
    VertexCompile = 1,   ///< Vertex shader failed to compile.
    GeometryCompile = 2, ///< Geometry shader failed to compile.
    FragmentCompile = 3, ///< Fragment shader failed to compile.
    Link = 4,            ///< Linking the program failed.
};

/// RAII wrapper for a GL shader program (linked vertex + fragment shaders).
///
/// Movable but not copyable; the GL program is deleted on destruction.
/// Requires a current GL context.
class ShaderProgram {
   public:
    /// Compile `vertexSource` + `fragmentSource` and link them into a program.
    /// On any failure returns a typed error whose message embeds the driver's
    /// diagnostics (see file comment) and whose code is a ShaderError value.
    static data::Result<ShaderProgram> create(std::string_view vertexSource,
                                              std::string_view fragmentSource);

    /// Compile `vertexSource` + `geometrySource` + `fragmentSource` and link
    /// them into a program. `geometrySource` must declare a `layout(...) in`
    /// and `layout(...) out` matching the draw and its output primitive type.
    /// On any failure returns a typed error whose message embeds the driver's
    /// diagnostics and whose code is a ShaderError value.
    static data::Result<ShaderProgram> createWithGeometry(
        std::string_view vertexSource, std::string_view geometrySource,
        std::string_view fragmentSource);

    /// Compile `vertexSource` + `geometrySource` + `fragmentSource`, declare
    /// the transform-feedback varyings `varyings` (each a GLSL varying name
    /// written by the geometry stage), and link them into a program. The
    /// varyings are captured with GL_INTERLEAVED_ATTRIBS into the single
    /// transform-feedback buffer bound to index 0 (the caller begins capture
    /// with the primitive mode matching the geometry stage's output, e.g.
    /// GL_TRIANGLES, via core::TransformFeedback::begin).
    ///
    /// This is used by the SliceRenderer cross-section gate (T11, FR-render.4)
    /// to capture the on-plane vertices a geometry shader emits. On any failure
    /// returns a typed error whose message embeds the driver's diagnostics and
    /// whose code is a ShaderError value.
    static data::Result<ShaderProgram> createWithTransformFeedback(
        std::string_view vertexSource, std::string_view geometrySource,
        std::string_view fragmentSource,
        const std::vector<std::string>& varyings);

    /// Load a GLSL source file from disk. On success returns the file's
    /// exact contents (used by the `.glsl` shader files in `render/shaders/`,
    /// SPEC §9 V2.6). Preserves line numbers so diagnostics keep their
    /// `ERROR: 0:N` form. On failure returns a typed error (code 1).
    static data::Result<std::string> loadSourceFile(
        const std::filesystem::path& path);

    /// Load GLSL source from `.glsl` files and compile/link them into a
    /// program. Convenience wrappers around `loadSourceFile` + `create*`
    /// (SPEC §9 V2.6: shaders live in `.glsl` files for syntax highlighting).
    static data::Result<ShaderProgram> createFromFiles(
        const std::filesystem::path& vertexPath,
        const std::filesystem::path& fragmentPath);

    /// Load GLSL source from `.glsl` files (including geometry) and link.
    static data::Result<ShaderProgram> createWithGeometryFromFiles(
        const std::filesystem::path& vertexPath,
        const std::filesystem::path& geometryPath,
        const std::filesystem::path& fragmentPath);

    /// Load GLSL source from `.glsl` files and link with transform-feedback
    /// varyings. File-backed variant of `createWithTransformFeedback`.
    static data::Result<ShaderProgram> createWithTransformFeedbackFromFiles(
        const std::filesystem::path& vertexPath,
        const std::filesystem::path& geometryPath,
        const std::filesystem::path& fragmentPath,
        const std::vector<std::string>& varyings);

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    ~ShaderProgram();

    /// Install the program for drawing (glUseProgram).
    void use() const noexcept;

    /// Uninstall (glUseProgram(0)).
    void unuse() const noexcept;

    /// Set a 1-component int uniform. The program must be in use.
    void setUniformInt(std::string_view name,
                       std::int32_t value) const noexcept;

    /// Set a 1-component float uniform. The program must be in use.
    void setUniformFloat(std::string_view name, float value) const noexcept;

    /// Set a vec3 uniform. The program must be in use.
    void setUniformVec3(std::string_view name,
                        const glm::vec3& value) const noexcept;

    /// Set a vec4 uniform. The program must be in use.
    void setUniformVec4(std::string_view name,
                        const glm::vec4& value) const noexcept;

    /// Set the first `count` elements of a float array uniform. The program
    /// must be in use.
    void setUniformFloatArray(std::string_view name, const float* values,
                              std::size_t count) const noexcept;

    /// Set the first `count` elements of a vec4 array uniform. The program
    /// must be in use.
    void setUniformVec4Array(std::string_view name, const glm::vec4* values,
                             std::size_t count) const noexcept;

    /// Set a mat4 uniform. The program must be in use.
    void setUniformMat4(std::string_view name,
                        const glm::mat4& value) const noexcept;

    /// The GL object name (non-zero for a valid generated name).
    std::uint32_t id() const noexcept {
        return id_;
    }

    /// True if this object owns a GL program name (id != 0).
    bool valid() const noexcept {
        return id_ != 0u;
    }

   private:
    explicit ShaderProgram(std::uint32_t id) noexcept : id_(id) {}

    std::uint32_t id_{0u};
};

} // namespace re::core
