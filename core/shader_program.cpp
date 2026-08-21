// core/shader_program.cpp — RAII GLSL shader program implementation.

#include "core/shader_program.hpp"

#include <glad/gl.h>

#include <string>
#include <utility>
#include <vector>

namespace re::core {

namespace {

/// Normalize a driver info log so every non-empty diagnostic line carries the
/// project's golden diagnostic prefix "ERROR: " (SPEC S4 FR-core.2) followed
/// by the driver's own location + message, e.g.
///
///   ERROR: 0:7(27): error: `glibberish' undeclared
///
/// The offending line is therefore always reported in the "0:N" location
/// form. An empty log yields a single placeholder line.
std::string normalizeInfoLog(std::string_view infoLog) {
    if (infoLog.empty()) {
        return "ERROR: <no compiler diagnostics>\n";
    }
    std::string out;
    out.reserve(infoLog.size() + 32);
    std::size_t lineStart = 0;
    while (lineStart < infoLog.size()) {
        const std::size_t nl = infoLog.find('\n', lineStart);
        const std::string_view line =
            (nl == std::string_view::npos)
                ? infoLog.substr(lineStart)
                : infoLog.substr(lineStart, nl - lineStart);
        if (!line.empty()) {
            out += "ERROR: ";
            out.append(line.data(), line.size());
            out += '\n';
        }
        if (nl == std::string_view::npos) {
            break;
        }
        lineStart = nl + 1;
    }
    return out;
}

/// Fetch the current info log of `object` (a shader or program).
std::string fetchInfoLog(std::uint32_t object, bool isProgram) {
    GLint logLen = 0;
    if (isProgram) {
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &logLen);
    } else {
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &logLen);
    }
    if (logLen <= 1) {
        return {};
    }
    std::vector<char> buffer(static_cast<std::size_t>(logLen), '\0');
    GLint written = 0;
    if (isProgram) {
        glGetProgramInfoLog(object, logLen, &written, buffer.data());
    } else {
        glGetShaderInfoLog(object, logLen, &written, buffer.data());
    }
    return std::string(buffer.data(), static_cast<std::size_t>(written));
}

/// Compile one shader stage. Returns the shader name on success; on failure
/// returns a typed error with `errorCode` and the normalized driver log.
data::Result<std::uint32_t> compileStage(GLenum stage, std::string_view source,
                                         int errorCode,
                                         const char* stageLabel) {
    if (glCreateShader == nullptr) {
        return data::makeError<std::uint32_t>(
            1, "ShaderProgram: no GL context (glCreateShader not loaded)");
    }
    const std::uint32_t shader = glCreateShader(stage);

    const char* sourcePtr = source.data();
    const GLint sourceLen = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &sourcePtr, &sourceLen);
    glCompileShader(shader);

    GLint compileStatus = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_TRUE) {
        return data::makeValue<std::uint32_t>(shader);
    }

    const std::string log = fetchInfoLog(shader, /*isProgram=*/false);
    const std::string message = std::string("ShaderProgram: ") + stageLabel +
                                " shader compile failed:\n" +
                                normalizeInfoLog(log);
    glDeleteShader(shader);
    return data::makeError<std::uint32_t>(errorCode, message);
}

} // namespace

data::Result<ShaderProgram> ShaderProgram::create(
    std::string_view vertexSource, std::string_view fragmentSource) {
    auto vertex =
        compileStage(GL_VERTEX_SHADER, vertexSource,
                     static_cast<int>(ShaderError::VertexCompile), "vertex");
    if (vertex.failed()) {
        return data::makeError<ShaderProgram>(vertex.error().code,
                                              vertex.error().message);
    }

    auto fragment = compileStage(GL_FRAGMENT_SHADER, fragmentSource,
                                 static_cast<int>(ShaderError::FragmentCompile),
                                 "fragment");
    if (fragment.failed()) {
        glDeleteShader(*vertex);
        return data::makeError<ShaderProgram>(fragment.error().code,
                                              fragment.error().message);
    }

    if (glCreateProgram == nullptr) {
        glDeleteShader(*vertex);
        glDeleteShader(*fragment);
        return data::makeError<ShaderProgram>(
            1, "ShaderProgram: no GL context (glCreateProgram not loaded)");
    }

    const std::uint32_t program = glCreateProgram();
    glAttachShader(program, *vertex);
    glAttachShader(program, *fragment);
    glLinkProgram(program);

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        const std::string log = fetchInfoLog(program, /*isProgram=*/true);
        const std::string message =
            std::string("ShaderProgram: link failed:\n") +
            normalizeInfoLog(log);
        glDeleteProgram(program);
        glDeleteShader(*vertex);
        glDeleteShader(*fragment);
        return data::makeError<ShaderProgram>(
            static_cast<int>(ShaderError::Link), message);
    }

    // Linked programs no longer need their attached shader objects.
    glDeleteShader(*vertex);
    glDeleteShader(*fragment);
    return data::makeValue<ShaderProgram>(ShaderProgram(program));
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept : id_(other.id_) {
    other.id_ = 0u;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        if (id_ != 0u) {
            glDeleteProgram(id_);
        }
        id_ = other.id_;
        other.id_ = 0u;
    }
    return *this;
}

ShaderProgram::~ShaderProgram() {
    if (id_ != 0u) {
        glDeleteProgram(id_);
    }
}

void ShaderProgram::use() const noexcept {
    glUseProgram(id_);
}

void ShaderProgram::unuse() const noexcept {
    glUseProgram(0u);
}

void ShaderProgram::setUniformInt(std::string_view name,
                                  std::int32_t value) const noexcept {
    const std::string nameString(name);
    glUniform1i(glGetUniformLocation(id_, nameString.c_str()), value);
}

void ShaderProgram::setUniformVec3(std::string_view name,
                                   const glm::vec3& value) const noexcept {
    const std::string nameString(name);
    glUniform3fv(glGetUniformLocation(id_, nameString.c_str()), 1, &value.x);
}

void ShaderProgram::setUniformVec4(std::string_view name,
                                   const glm::vec4& value) const noexcept {
    const std::string nameString(name);
    glUniform4fv(glGetUniformLocation(id_, nameString.c_str()), 1, &value.x);
}

void ShaderProgram::setUniformMat4(std::string_view name,
                                   const glm::mat4& value) const noexcept {
    const std::string nameString(name);
    glUniformMatrix4fv(glGetUniformLocation(id_, nameString.c_str()), 1,
                       GL_FALSE, &value[0][0]);
}

} // namespace re::core
