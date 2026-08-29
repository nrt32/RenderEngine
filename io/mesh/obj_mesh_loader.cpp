// io/mesh/obj_mesh_loader.cpp — OBJ-style mesh loader implementation
// (FR-io.1, FR-io.4).

#include "io/mesh/obj_mesh_loader.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <glm/vec3.hpp>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace re::io {
namespace {

constexpr std::uint64_t kHeaderSlackBytes = 64ULL * 1024ULL;
constexpr std::uint64_t kMaxFileBytes = 512ULL * 512ULL * 512ULL * 8ULL + kHeaderSlackBytes;

std::string trimmed(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = line.find_last_not_of(" \t\r\n");
    return line.substr(first, last - first + 1);
}

std::string stripComment(const std::string& line) {
    const std::size_t hash = line.find('#');
    return hash == std::string::npos ? line : line.substr(0, hash);
}

// Parse the leading integer of a face index token ("i", "i/t", "i//n" or
// "i/t/n"). Accepts positive 1-based indices and negative relative indices
// (-1 → last vertex) per OBJ spec. Returns false if the token does not start
// with a non-zero integer. VG5: strtol ERANGE check + errno reset — giant
// indices like 99999999999999999999 must be rejected as typed error, not
// silently wrapped, and errno must be cleared before the call so a prior
// ERANGE does not pollute later parses. Negative values are returned as-is
// and resolved after the whole file is parsed relative to final vertex count.
bool parseFaceVertexIndex(const std::string& token, std::int64_t& out) {
    if (token.empty()) {
        return false;
    }
    const char* begin = token.c_str();
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(begin, &end, 10);
    if (errno == ERANGE) {
        return false;
    }
    if (end == begin || parsed == 0) {
        return false;
    }
    if (parsed > static_cast<long>(std::numeric_limits<std::int32_t>::max()) ||
        parsed < static_cast<long>(std::numeric_limits<std::int32_t>::min())) {
        return false;
    }
    out = static_cast<std::int64_t>(parsed);
    return true;
}

} // namespace

data::Result<data::Mesh> loadObjMesh(const std::string& path) {
    // Host file-size pre-probe before slurp (mirror NRRD, T11b).
    {
        std::error_code ec;
        const auto fileSize = std::filesystem::file_size(path, ec);
        if (!ec && static_cast<std::uint64_t>(fileSize) > kMaxFileBytes) {
            spdlog::warn(
                "OBJ loader: '{}' file size {} exceeds cap {} — rejecting before slurp",
                path, static_cast<std::uint64_t>(fileSize), kMaxFileBytes);
            return data::makeError<data::Mesh>(
                data::ErrorDomain::MeshIo,
                static_cast<int>(MeshLoadError::BudgetExceeded),
                "OBJ loader: '" + path + "' file size " +
                    std::to_string(static_cast<std::uint64_t>(fileSize)) + " exceeds cap " +
                    std::to_string(kMaxFileBytes));
        }
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return data::makeError<data::Mesh>(
            data::ErrorDomain::MeshIo,
            static_cast<int>(MeshLoadError::FileOpen),
            "OBJ loader: cannot open file '" + path + "'");
    }

    std::vector<glm::vec3> positions;
    std::vector<std::int64_t> rawIndices;

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string content = stripComment(trimmed(line));
        if (content.empty()) {
            continue;
        }

        std::istringstream tokens(content);
        std::string keyword;
        tokens >> keyword;

        if (keyword == "v") {
            // Vertex position: three floats ("x y z"); an optional 4th
            // homogeneous component is ignored. Probe kMaxFileBytes before every
            // positions.emplace_back to guard unbounded vertex streams (T11b
            // 512^3*8+64KiB cap): reject when vertex memory or file size would
            // exceed the absolute budget, mirroring NRRD's pre-probe discipline.
            {
                const std::uint64_t posBytes =
                    static_cast<std::uint64_t>(positions.size() + 1) * sizeof(glm::vec3);
                if (posBytes > kMaxFileBytes) {
                    return data::makeError<data::Mesh>(
                        data::ErrorDomain::MeshIo,
                        static_cast<int>(MeshLoadError::BudgetExceeded),
                        "OBJ loader: '" + path + "' vertex budget exceeds cap " +
                            std::to_string(kMaxFileBytes));
                }
                std::error_code ec;
                const auto sz = std::filesystem::file_size(path, ec);
                if (!ec && static_cast<std::uint64_t>(sz) > kMaxFileBytes) {
                    return data::makeError<data::Mesh>(
                        data::ErrorDomain::MeshIo,
                        static_cast<int>(MeshLoadError::BudgetExceeded),
                        "OBJ loader: '" + path + "' exceeds cap during vertex stream");
                }
            }
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!(tokens >> x) || !(tokens >> y) || !(tokens >> z)) {
                return data::makeError<data::Mesh>(
                    data::ErrorDomain::MeshIo,
                    static_cast<int>(MeshLoadError::VertexParse),
                    "OBJ loader: '" + path + "' line " +
                        std::to_string(lineNumber) +
                        ": malformed 'v' line (expected three floats)");
            }
            positions.emplace_back(x, y, z);
        } else if (keyword == "f") {
            // Face: 1-based vertex indices, each possibly "i", "i/t", "i//n"
            // or "i/t/n". Polygons are fan-triangulated unbounded (T11b removes
            // kMaxFaceVertices=64 cap); negative relative indices accepted.
            std::vector<std::int64_t> face;
            std::string token;
            while (tokens >> token) {
                std::int64_t index = 0;
                if (!parseFaceVertexIndex(token, index)) {
                    return data::makeError<data::Mesh>(
                        data::ErrorDomain::MeshIo,
                        static_cast<int>(MeshLoadError::FaceParse),
                        "OBJ loader: '" + path + "' line " +
                            std::to_string(lineNumber) +
                            ": malformed face index '" + token + "' (expected 1-based or negative relative)");
                }
                face.push_back(index);
            }
            if (face.size() < 3) {
                return data::makeError<data::Mesh>(
                    data::ErrorDomain::MeshIo,
                    static_cast<int>(MeshLoadError::FaceParse),
                    "OBJ loader: '" + path + "' line " +
                        std::to_string(lineNumber) +
                        ": face has fewer than 3 vertices");
            }
            // Fan triangulation around the face's first vertex (unbounded).
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                rawIndices.push_back(face[0]);
                rawIndices.push_back(face[i]);
                rawIndices.push_back(face[i + 1]);
            }
        }
        // Any other element (vt, vn, g, o, s, usemtl, mtllib, l, p, ...) is
        // ignored: v1 needs geometry only.
    }

    if (positions.empty()) {
        return data::makeError<data::Mesh>(
            data::ErrorDomain::MeshIo,
            static_cast<int>(MeshLoadError::NoVertices),
            "OBJ loader: '" + path + "' contains no 'v' vertices");
    }
    if (rawIndices.empty()) {
        return data::makeError<data::Mesh>(
            data::ErrorDomain::MeshIo,
            static_cast<int>(MeshLoadError::NoFaces),
            "OBJ loader: '" + path + "' contains no 'f' faces");
    }

    // Resolve every face index (positive 1-based or negative relative) against
    // final vertex count and validate AFTER whole file is parsed (faces may
    // legally precede vertices). Negative -1 → last vertex, -2 → second last.
    // Nothing partial escapes: the Mesh is only built once validation passes.
    std::vector<std::uint32_t> indices;
    indices.reserve(rawIndices.size());
    for (const std::int64_t raw : rawIndices) {
        std::int64_t resolved = raw;
        if (raw < 0) {
            resolved = static_cast<std::int64_t>(positions.size()) + raw + 1;
        }
        if (resolved <= 0 || resolved > static_cast<std::int64_t>(positions.size())) {
            return data::makeError<data::Mesh>(
                data::ErrorDomain::MeshIo,
                static_cast<int>(MeshLoadError::IndexRange),
                "OBJ loader: '" + path + "' references out-of-range vertex index " +
                    std::to_string(raw) + " (resolved " + std::to_string(resolved) +
                    ", file has " + std::to_string(positions.size()) + " vertices)");
        }
        indices.push_back(static_cast<std::uint32_t>(resolved));
    }

    // Convert to 0-based indices.
    for (std::uint32_t& index : indices) {
        --index;
    }

    return data::makeValue<data::Mesh>(
        data::Mesh::fromTriangles(std::move(positions), std::move(indices)));
}

} // namespace re::io