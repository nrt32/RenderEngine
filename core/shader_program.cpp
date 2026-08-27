// core/shader_program.cpp — RAII GLSL shader program implementation.

#include "core/shader_program.hpp"

#include <glad/gl.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace re::core {

namespace {

/// Normalize a driver info log so every non-empty diagnostic line carries the
/// project's golden diagnostic prefix "ERROR: " (SPEC §4 FR-core.2) followed
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

/// A single shader stage to compile: its GL type + source.
struct StageSource {
    GLenum type;
    std::string_view source;
};

/// Compile and link `stages` into a program. When `varyings` is non-empty, the
/// transform-feedback varyings are declared (GL_INTERLEAVED_ATTRIBS) before
/// linking. Returns the linked program name on success.
data::Result<std::uint32_t> createAndLink(
    const std::vector<StageSource>& stages,
    const std::vector<std::string>& varyings) {
    // Compile each stage first so a failure can clean up all created objects.
    std::vector<std::uint32_t> shaderObjects;
    for (const StageSource& stage : stages) {
        int errorCode = 1;
        const char* stageLabel = "shader";
        if (stage.type == GL_VERTEX_SHADER) {
            errorCode = static_cast<int>(ShaderError::VertexCompile);
            stageLabel = "vertex";
        } else if (stage.type == GL_GEOMETRY_SHADER) {
            errorCode = static_cast<int>(ShaderError::GeometryCompile);
            stageLabel = "geometry";
        } else if (stage.type == GL_FRAGMENT_SHADER) {
            errorCode = static_cast<int>(ShaderError::FragmentCompile);
            stageLabel = "fragment";
        }
        auto compiled =
            compileStage(stage.type, stage.source, errorCode, stageLabel);
        if (compiled.failed()) {
            for (const std::uint32_t s : shaderObjects) {
                glDeleteShader(s);
            }
            return data::makeError<std::uint32_t>(compiled.error().code,
                                                  compiled.error().message);
        }
        shaderObjects.push_back(*compiled);
    }

    if (glCreateProgram == nullptr) {
        // Unusual partial-load edge case: the stages above compiled, so free
        // them before reporting the missing context (no leaks on any path).
        for (const std::uint32_t s : shaderObjects) {
            glDeleteShader(s);
        }
        return data::makeError<std::uint32_t>(
            1, "ShaderProgram: no GL context (glCreateProgram not loaded)");
    }

    const std::uint32_t program = glCreateProgram();
    for (const std::uint32_t s : shaderObjects) {
        glAttachShader(program, s);
    }

    if (!varyings.empty()) {
        std::vector<const char*> varyingPtrs;
        varyingPtrs.reserve(varyings.size());
        for (const std::string& v : varyings) {
            varyingPtrs.push_back(v.c_str());
        }
        glTransformFeedbackVaryings(program,
                                    static_cast<GLsizei>(varyingPtrs.size()),
                                    varyingPtrs.data(), GL_INTERLEAVED_ATTRIBS);
    }

    glLinkProgram(program);

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        const std::string log = fetchInfoLog(program, /*isProgram=*/true);
        const std::string message =
            std::string("ShaderProgram: link failed:\n") +
            normalizeInfoLog(log);
        glDeleteProgram(program);
        for (const std::uint32_t s : shaderObjects) {
            glDeleteShader(s);
        }
        return data::makeError<std::uint32_t>(
            static_cast<int>(ShaderError::Link), message);
    }

    // Linked programs no longer need their attached shader objects.
    for (const std::uint32_t s : shaderObjects) {
        glDeleteShader(s);
    }
    return data::makeValue<std::uint32_t>(program);
}

data::Result<ShaderProgram> ShaderProgram::create(
    std::string_view vertexSource, std::string_view fragmentSource) {
    std::vector<StageSource> stages = {
        StageSource{GL_VERTEX_SHADER, vertexSource},
        StageSource{GL_FRAGMENT_SHADER, fragmentSource},
    };
    auto program = createAndLink(stages, {});
    if (program.failed()) {
        return data::makeError<ShaderProgram>(program.error().code,
                                              program.error().message);
    }
    return data::makeValue<ShaderProgram>(ShaderProgram(*program));
}

data::Result<ShaderProgram> ShaderProgram::createWithGeometry(
    std::string_view vertexSource, std::string_view geometrySource,
    std::string_view fragmentSource) {
    std::vector<StageSource> stages = {
        StageSource{GL_VERTEX_SHADER, vertexSource},
        StageSource{GL_GEOMETRY_SHADER, geometrySource},
        StageSource{GL_FRAGMENT_SHADER, fragmentSource},
    };
    auto program = createAndLink(stages, {});
    if (program.failed()) {
        return data::makeError<ShaderProgram>(program.error().code,
                                              program.error().message);
    }
    return data::makeValue<ShaderProgram>(ShaderProgram(*program));
}

data::Result<ShaderProgram> ShaderProgram::createWithTransformFeedback(
    std::string_view vertexSource, std::string_view geometrySource,
    std::string_view fragmentSource, const std::vector<std::string>& varyings) {
    std::vector<StageSource> stages = {
        StageSource{GL_VERTEX_SHADER, vertexSource},
        StageSource{GL_GEOMETRY_SHADER, geometrySource},
        StageSource{GL_FRAGMENT_SHADER, fragmentSource},
    };
    auto program = createAndLink(stages, varyings);
    if (program.failed()) {
        return data::makeError<ShaderProgram>(program.error().code,
                                              program.error().message);
    }
    return data::makeValue<ShaderProgram>(ShaderProgram(*program));
}

namespace {

data::Result<std::string> preprocessIncludes(const std::string& source,
                                             const std::filesystem::path& baseDir,
                                             int depth = 0) {
    if (depth > 8) {
        return data::makeError<std::string>(
            1, "ShaderProgram: include depth exceeded");
    }
    std::string out;
    out.reserve(source.size() * 2);
    std::size_t pos = 0;
    while (pos < source.size()) {
        std::size_t nl = source.find('\n', pos);
        std::string_view line = (nl == std::string::npos)
                                    ? std::string_view(source.data() + pos,
                                                         source.size() - pos)
                                    : std::string_view(source.data() + pos,
                                                         nl - pos);
        // Trim leading whitespace for include detection.
        std::size_t first = line.find_first_not_of(" \t\r");
        bool isInclude = false;
        std::string includePath;
        if (first != std::string_view::npos &&
            line.substr(first, 8) == "#include") {
            std::size_t q1 = line.find('"', first + 8);
            std::size_t q2 = (q1 == std::string_view::npos)
                                 ? std::string_view::npos
                                 : line.find('"', q1 + 1);
            if (q1 != std::string_view::npos && q2 != std::string_view::npos) {
                includePath = std::string(line.substr(q1 + 1, q2 - q1 - 1));
                isInclude = true;
            }
        }
        if (isInclude) {
            std::filesystem::path inc = baseDir / includePath;
            std::ifstream incFile(inc, std::ios::in | std::ios::binary);
            if (!incFile) {
                return data::makeError<std::string>(
                    1, "ShaderProgram: failed to open include file: " + inc.string());
            }
            std::ostringstream incBuf;
            incBuf << incFile.rdbuf();
            if (incFile.bad()) {
                return data::makeError<std::string>(
                    1, "ShaderProgram: failed to read include file: " + inc.string());
            }
            std::string incContent = incBuf.str();
            auto pre = preprocessIncludes(incContent, inc.parent_path(), depth + 1);
            if (pre.failed()) {
                return pre;
            }
            out += *pre;
            out += '\n';
        } else {
            out.append(line.data(), line.size());
            out += '\n';
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return data::makeValue<std::string>(std::move(out));
}

} // namespace

data::Result<std::string> ShaderProgram::loadSourceFile(
    const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        return data::makeError<std::string>(
            1, "ShaderProgram: failed to open shader file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) {
        return data::makeError<std::string>(
            1, "ShaderProgram: failed to read shader file: " + path.string());
    }
    std::string raw = buffer.str();
    auto pre = preprocessIncludes(raw, path.parent_path(), 0);
    if (pre.failed()) {
        return pre;
    }
    return data::makeValue<std::string>(std::move(*pre));
}

data::Result<ShaderProgram> ShaderProgram::createFromFiles(
    const std::filesystem::path& vertexPath,
    const std::filesystem::path& fragmentPath) {
    auto vs = loadSourceFile(vertexPath);
    if (vs.failed()) {
        return data::makeError<ShaderProgram>(vs.error().code, vs.error().message);
    }
    auto fs = loadSourceFile(fragmentPath);
    if (fs.failed()) {
        return data::makeError<ShaderProgram>(fs.error().code, fs.error().message);
    }
    return create(*vs, *fs);
}

data::Result<ShaderProgram> ShaderProgram::createWithGeometryFromFiles(
    const std::filesystem::path& vertexPath,
    const std::filesystem::path& geometryPath,
    const std::filesystem::path& fragmentPath) {
    auto vs = loadSourceFile(vertexPath);
    if (vs.failed()) {
        return data::makeError<ShaderProgram>(vs.error().code, vs.error().message);
    }
    auto gs = loadSourceFile(geometryPath);
    if (gs.failed()) {
        return data::makeError<ShaderProgram>(gs.error().code, gs.error().message);
    }
    auto fs = loadSourceFile(fragmentPath);
    if (fs.failed()) {
        return data::makeError<ShaderProgram>(fs.error().code, fs.error().message);
    }
    return createWithGeometry(*vs, *gs, *fs);
}

data::Result<ShaderProgram> ShaderProgram::createWithTransformFeedbackFromFiles(
    const std::filesystem::path& vertexPath,
    const std::filesystem::path& geometryPath,
    const std::filesystem::path& fragmentPath,
    const std::vector<std::string>& varyings) {
    auto vs = loadSourceFile(vertexPath);
    if (vs.failed()) {
        return data::makeError<ShaderProgram>(vs.error().code, vs.error().message);
    }
    auto gs = loadSourceFile(geometryPath);
    if (gs.failed()) {
        return data::makeError<ShaderProgram>(gs.error().code, gs.error().message);
    }
    auto fs = loadSourceFile(fragmentPath);
    if (fs.failed()) {
        return data::makeError<ShaderProgram>(fs.error().code, fs.error().message);
    }
    return createWithTransformFeedback(*vs, *gs, *fs, varyings);
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

void ShaderProgram::setUniformFloat(std::string_view name,
                                    float value) const noexcept {
    const std::string nameString(name);
    glUniform1f(glGetUniformLocation(id_, nameString.c_str()), value);
}

void ShaderProgram::setUniformVec2(std::string_view name,
                                   const glm::vec2& value) const noexcept {
    const std::string nameString(name);
    glUniform2fv(glGetUniformLocation(id_, nameString.c_str()), 1, &value.x);
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

void ShaderProgram::setUniformFloatArray(std::string_view name,
                                         const float* values,
                                         std::size_t count) const noexcept {
    const std::string nameString(name);
    glUniform1fv(glGetUniformLocation(id_, nameString.c_str()),
                 static_cast<GLsizei>(count), values);
}

void ShaderProgram::setUniformVec4Array(std::string_view name,
                                        const glm::vec4* values,
                                        std::size_t count) const noexcept {
    const std::string nameString(name);
    glUniform4fv(glGetUniformLocation(id_, nameString.c_str()),
                 static_cast<GLsizei>(count), &values[0].x);
}

void ShaderProgram::setUniformMat4(std::string_view name,
                                   const glm::mat4& value) const noexcept {
    const std::string nameString(name);
    glUniformMatrix4fv(glGetUniformLocation(id_, nameString.c_str()), 1,
                       GL_FALSE, &value[0][0]);
}

} // namespace re::core
