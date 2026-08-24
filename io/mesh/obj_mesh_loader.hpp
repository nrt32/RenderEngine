#pragma once

// io/mesh/obj_mesh_loader.hpp — OBJ-style mesh loader (FR-io.1, FR-io.4).
//
// io/ is GL-free: the loader parses a Wavefront OBJ text file into a
// data::Mesh (CPU container). Supported syntax:
//   - "v x y z" vertices (an optional 4th component is ignored);
//   - "f i j k ..." faces with 1-based positive vertex indices, in any of the
//     forms i, i/t, i//n, i/t/n (texture/normal components are ignored);
//     polygons with more than 3 vertices are fan-triangulated;
//   - "#" comment lines and blank lines are skipped; unknown element lines
//     (vt, vn, g, o, s, usemtl, mtllib, l, p, ...) are ignored.
//
// Errors (FR-io.4) are reported as a typed data::Result carrying a
// MeshLoadError code and a message that names the offending file and line; no
// exceptions are thrown and no partially-built Mesh ever escapes (the Mesh is
// constructed only after the whole file has been validated).

#include <string>

#include "data/mesh.hpp"
#include "data/result.hpp"

namespace re::io {

/// Error codes carried by data::Error::code for OBJ load failures. Typed and
/// enumerated (never thrown): callers branch on the code instead of parsing
/// messages, and the numeric values are stable API — tests assert them.
enum class MeshLoadError : int {
    FileOpen = 1,    ///< The file could not be opened for reading.
    VertexParse = 2, ///< A "v" line does not contain three valid floats.
    FaceParse = 3,   ///< An "f" line does not contain >= 3 valid indices.
    IndexRange = 4,  ///< A face references a vertex index out of range.
    NoVertices = 5,  ///< The file contains no "v" vertices.
    NoFaces = 6,     ///< The file contains no "f" faces.
};

/// Load an OBJ-style mesh from `path` into a `data::Mesh` (FR-io.1). Returns
/// a typed error (FR-io.4) on any malformed input; a failed result never
/// carries a partial Mesh.
data::Result<data::Mesh> loadObjMesh(const std::string& path);

} // namespace re::io