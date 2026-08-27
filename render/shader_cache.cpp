// render/shader_cache.cpp — single lazy program cache implementation
//
// This file provides the one place where RE_SHADER_DIR is resolved and the
// file-backed ShaderProgram factories are invoked. Every technique renderer
// previously duplicated these steps; now they delegate to this shared type.

#include "render/shader_cache.hpp"

#include <filesystem>

#include <spdlog/spdlog.h>

#include "core/shader_program.hpp"

namespace re::render {

data::Result<core::ShaderProgram*> LazyProgramCache::getOrLoadFromFiles(
    const std::filesystem::path& vertPath,
    const std::filesystem::path& fragPath) {
    if (program_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*program_);
    }
    // RI7: the baked RE_SHADER_DIR is an absolute source path and therefore
    // non-relocatable; a binary relocated without the source tree will fail
    // to resolve the shader files. Emit a warning when the baked directory
    // does not exist so the failure is evidence-rich rather than silent —
    // the gate asserts that installed binaries copy render/shaders beside the
    // executable or provide an install rule; until then the baked absolute
    // path is intentional and the warning preserves the evidence.
    const std::filesystem::path dir = vertPath.parent_path();
    if (!std::filesystem::exists(dir)) {
        spdlog::warn(
            "LazyProgramCache: baked RE_SHADER_DIR '{}' does not exist — "
            "shader files are non-relocatable (copy render/shaders beside "
            "the binary or install them to the baked path)",
            dir.string());
    }
    auto program = core::ShaderProgram::createFromFiles(vertPath, fragPath);
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                    program.error().message);
    }
    program_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*program_);
}

data::Result<core::ShaderProgram*> LazyProgramCache::getOrLoadWithGeometryFromFiles(
    const std::filesystem::path& vertPath,
    const std::filesystem::path& geomPath,
    const std::filesystem::path& fragPath) {
    if (program_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*program_);
    }
    const std::filesystem::path dir = vertPath.parent_path();
    if (!std::filesystem::exists(dir)) {
        spdlog::warn(
            "LazyProgramCache: baked RE_SHADER_DIR '{}' does not exist — "
            "shader files are non-relocatable (copy render/shaders beside "
            "the binary or install them to the baked path)",
            dir.string());
    }
    auto program = core::ShaderProgram::createWithGeometryFromFiles(
        vertPath, geomPath, fragPath);
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                    program.error().message);
    }
    program_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*program_);
}

data::Result<core::ShaderProgram*> LazyProgramCache::getOrLoadWithTransformFeedbackFromFiles(
    const std::filesystem::path& vertPath,
    const std::filesystem::path& geomPath,
    const std::filesystem::path& fragPath,
    const std::vector<std::string>& varyings) {
    if (program_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*program_);
    }
    const std::filesystem::path dir = vertPath.parent_path();
    if (!std::filesystem::exists(dir)) {
        spdlog::warn(
            "LazyProgramCache: baked RE_SHADER_DIR '{}' does not exist — "
            "shader files are non-relocatable (copy render/shaders beside "
            "the binary or install them to the baked path)",
            dir.string());
    }
    auto program = core::ShaderProgram::createWithTransformFeedbackFromFiles(
        vertPath, geomPath, fragPath, varyings);
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                    program.error().message);
    }
    program_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*program_);
}

} // namespace re::render
