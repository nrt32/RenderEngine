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

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <string_view>

#include "data/result.hpp"

namespace re::core {

/// Enumerated diagnostics codes carried by data::Error::code for shader
/// failures (typed, SPEC S5). Public API: lives in the header.
enum class ShaderError : int {
    VertexCompile = 1,   ///< Vertex shader failed to compile.
    FragmentCompile = 2, ///< Fragment shader failed to compile.
    Link = 3,            ///< Linking the program failed.
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

    /// Set a vec3 uniform. The program must be in use.
    void setUniformVec3(std::string_view name,
                        const glm::vec3& value) const noexcept;

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
