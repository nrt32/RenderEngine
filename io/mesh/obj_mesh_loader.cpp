// io/mesh/obj_mesh_loader.cpp — OBJ-style mesh loader implementation
// (FR-io.1, FR-io.4).

#include "io/mesh/obj_mesh_loader.hpp"

#include <cstdlib>
#include <fstream>
#include <glm/vec3.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace re::io {
namespace {

// The maximum number of vertices a single face may carry before
// fan-triangulation. 4 is the OBJ norm (quads); anything larger is still
// accepted via fan triangulation, so this only bounds a single line's tokens.
constexpr std::size_t kMaxFaceVertices = 64;

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
// "i/t/n"). Returns false if the token does not start with a positive
// integer (negative/relative indices are rejected).
bool parseFaceVertexIndex(const std::string& token, std::uint32_t& out) {
    if (token.empty()) {
        return false;
    }
    const char* begin = token.c_str();
    char* end = nullptr;
    const long parsed = std::strtol(begin, &end, 10);
    if (end == begin || parsed <= 0) {
        return false;
    }
    out = static_cast<std::uint32_t>(parsed);
    return true;
}

} // namespace

data::Result<data::Mesh> loadObjMesh(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return data::makeError<data::Mesh>(
            static_cast<int>(MeshLoadError::FileOpen),
            "OBJ loader: cannot open file '" + path + "'");
    }

    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;

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
            // homogeneous component is ignored.
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!(tokens >> x) || !(tokens >> y) || !(tokens >> z)) {
                return data::makeError<data::Mesh>(
                    static_cast<int>(MeshLoadError::VertexParse),
                    "OBJ loader: '" + path + "' line " +
                        std::to_string(lineNumber) +
                        ": malformed 'v' line (expected three floats)");
            }
            positions.emplace_back(x, y, z);
        } else if (keyword == "f") {
            // Face: 1-based vertex indices, each possibly "i", "i/t", "i//n"
            // or "i/t/n". Polygons are fan-triangulated.
            std::vector<std::uint32_t> face;
            std::string token;
            while (tokens >> token) {
                if (face.size() >= kMaxFaceVertices) {
                    return data::makeError<data::Mesh>(
                        static_cast<int>(MeshLoadError::FaceParse),
                        "OBJ loader: '" + path + "' line " +
                            std::to_string(lineNumber) +
                            ": face exceeds the maximum vertex count");
                }
                std::uint32_t index = 0;
                if (!parseFaceVertexIndex(token, index)) {
                    return data::makeError<data::Mesh>(
                        static_cast<int>(MeshLoadError::FaceParse),
                        "OBJ loader: '" + path + "' line " +
                            std::to_string(lineNumber) +
                            ": malformed face index '" + token +
                            "' (expected a positive 1-based vertex index)");
                }
                face.push_back(index);
            }
            if (face.size() < 3) {
                return data::makeError<data::Mesh>(
                    static_cast<int>(MeshLoadError::FaceParse),
                    "OBJ loader: '" + path + "' line " +
                        std::to_string(lineNumber) +
                        ": face has fewer than 3 vertices");
            }
            // Fan triangulation around the face's first vertex.
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                indices.push_back(face[0]);
                indices.push_back(face[i]);
                indices.push_back(face[i + 1]);
            }
        }
        // Any other element (vt, vn, g, o, s, usemtl, mtllib, l, p, ...) is
        // ignored: v1 needs geometry only.
    }

    if (positions.empty()) {
        return data::makeError<data::Mesh>(
            static_cast<int>(MeshLoadError::NoVertices),
            "OBJ loader: '" + path + "' contains no 'v' vertices");
    }
    if (indices.empty()) {
        return data::makeError<data::Mesh>(
            static_cast<int>(MeshLoadError::NoFaces),
            "OBJ loader: '" + path + "' contains no 'f' faces");
    }

    // Validate every face index (1-based) against the vertex count AFTER the
    // whole file is parsed (faces may legally precede vertices in the file).
    // Nothing partial escapes: the Mesh is only built once validation passes.
    for (const std::uint32_t index : indices) {
        if (index == 0 || index > positions.size()) {
            return data::makeError<data::Mesh>(
                static_cast<int>(MeshLoadError::IndexRange),
                "OBJ loader: '" + path +
                    "' references out-of-range vertex index " +
                    std::to_string(index) + " (file has " +
                    std::to_string(positions.size()) + " vertices)");
        }
    }

    // Convert to 0-based indices.
    for (std::uint32_t& index : indices) {
        --index;
    }

    return data::makeValue<data::Mesh>(
        data::Mesh::fromTriangles(std::move(positions), std::move(indices)));
}

} // namespace re::io