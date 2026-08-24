// app/mpr_slice.cpp — MPR layout + slice-state scaffolding implementation
// (SPEC §3, FR-app.2; hosts the plane∩mesh helpers moved from the deleted
// app/mpr_contour.cpp in V3.8b T11 and the GPU slice-extraction display
// mapping shared by the sample and its gate tests).

#include "app/mpr_slice.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace re::app {

namespace {

/// Convert a straight RGBA color in [0, 1] to RGBA8 bytes (round-half-up,
/// matching the render/ convention: byte = round(c * 255 + 0.5)).
std::uint8_t toByte(float v) noexcept {
    return static_cast<std::uint8_t>(std::round(v * 255.0f));
}

} // namespace

std::array<MprViewport, 4> mprViewports(int windowWidth, int windowHeight) {
    // Split the window into four equal quadrants (SPEC FR-app.2). The 2x2 grid
    // order is T (top-left), C (top-right), S (bottom-left), 3D (bottom-right).
    // GL pixel coordinates: y = 0 is the bottom scanline, so the top half is
    // y = height/2 and the bottom half is y = 0.
    const int halfW = windowWidth / 2;
    const int halfH = windowHeight / 2;
    std::array<MprViewport, 4> views;
    views[0] = MprViewport{0, halfH, halfW, halfH};     // T  (top-left)
    views[1] = MprViewport{halfW, halfH, halfW, halfH}; // C  (top-right)
    views[2] = MprViewport{0, 0, halfW, halfH};         // S  (bottom-left)
    views[3] = MprViewport{halfW, 0, halfW, halfH};     // 3D (bottom-right)
    return views;
}

data::Image makeSliceImage(const data::VolumeDataset& dataset,
                           const volume::TransferFunction& tf, MprAxis axis,
                           std::uint32_t index) {
    // Dimensions of the slice rectangle over the two free axes (SPEC FR-app.2).
    std::int32_t width = 0;
    std::int32_t height = 0;
    switch (axis) {
        case MprAxis::Transverse: // over (X, Y) at constant Z
            width = static_cast<std::int32_t>(dataset.sizeX());
            height = static_cast<std::int32_t>(dataset.sizeY());
            break;
        case MprAxis::Coronal: // over (X, Z) at constant Y
            width = static_cast<std::int32_t>(dataset.sizeX());
            height = static_cast<std::int32_t>(dataset.sizeZ());
            break;
        case MprAxis::Sagittal: // over (Y, Z) at constant X
            width = static_cast<std::int32_t>(dataset.sizeY());
            height = static_cast<std::int32_t>(dataset.sizeZ());
            break;
    }

    std::vector<std::uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(width) *
                   static_cast<std::size_t>(height) * 4u);
    for (std::int32_t py = 0; py < height; ++py) {
        for (std::int32_t px = 0; px < width; ++px) {
            // Sample the voxel at the axis-specific coordinates (the free axes
            // map to the image's (x, y), the held axis is `index`).
            float density = 0.0f;
            switch (axis) {
                case MprAxis::Transverse:
                    density =
                        dataset.voxelAt(static_cast<std::uint32_t>(px),
                                        static_cast<std::uint32_t>(py), index);
                    break;
                case MprAxis::Coronal:
                    density =
                        dataset.voxelAt(static_cast<std::uint32_t>(px), index,
                                        static_cast<std::uint32_t>(py));
                    break;
                case MprAxis::Sagittal:
                    density =
                        dataset.voxelAt(index, static_cast<std::uint32_t>(px),
                                        static_cast<std::uint32_t>(py));
                    break;
            }
            const volume::RgbaColor c = tf.sample(density);
            pixels.push_back(toByte(c.r));
            pixels.push_back(toByte(c.g));
            pixels.push_back(toByte(c.b));
            pixels.push_back(toByte(c.a));
        }
    }
    return data::Image(width, height, 4, std::move(pixels));
}

SlicePlane slicePlane(MprAxis axis, const MprSliceState& state) {
    // The slice plane passes through the centers of the sliced voxel layer
    // (voxel centers sit at integer + 0.5 in the shared coordinate space).
    switch (axis) {
        case MprAxis::Transverse:
            return SlicePlane{MprAxis::Transverse,
                              static_cast<float>(state.transverseZ) + 0.5f};
        case MprAxis::Coronal:
            return SlicePlane{MprAxis::Coronal,
                              static_cast<float>(state.coronalY) + 0.5f};
        case MprAxis::Sagittal:
            return SlicePlane{MprAxis::Sagittal,
                              static_cast<float>(state.sagittalX) + 0.5f};
    }
    // Unreachable (the enum has exactly three values); keep the compiler
    // satisfied on all paths.
    return SlicePlane{MprAxis::Transverse, 0.0f};
}

std::pair<std::uint32_t, std::uint32_t> sliceFreeAxes(
    const data::VolumeDataset& dataset, MprAxis axis) {
    switch (axis) {
        case MprAxis::Transverse: // display over (X, Y) at constant Z
            return {dataset.sizeX(), dataset.sizeY()};
        case MprAxis::Coronal: // display over (X, Z) at constant Y
            return {dataset.sizeX(), dataset.sizeZ()};
        case MprAxis::Sagittal: // display over (Y, Z) at constant X
            return {dataset.sizeY(), dataset.sizeZ()};
    }
    // Unreachable (the enum has exactly three values); keep the compiler
    // satisfied on all paths.
    return {1u, 1u};
}

glm::mat4 sliceVolumeModel(const data::VolumeDataset& dataset, MprAxis axis) {
    // Step 1 — scale each VOLUME axis by max(dim - 1, 1): model coordinate
    // i/(dim-1) then lands exactly on continuous index i after the shader's
    // idx = modelPos*(dim-1) conversion. The max(.., 1) floor keeps a
    // single-voxel axis non-degenerate (a zero scale would make the model
    // matrix singular and its inverse — which the extraction shader uses —
    // undefined). Step 2 — translate by (0.5, 0.5, 0.5) so voxel-center
    // index i sits at display coordinate i + 0.5, matching makeSliceImage's
    // pixel-center convention.
    const glm::vec3 scales(std::max(dataset.sizeX() - 1u, 1u) * 1.0f,
                           std::max(dataset.sizeY() - 1u, 1u) * 1.0f,
                           std::max(dataset.sizeZ() - 1u, 1u) * 1.0f);
    const glm::mat4 place =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 0.5f)) *
        glm::scale(glm::mat4(1.0f), scales);

    // Step 3 — the axis permutation into display space, identical to the
    // contour overlay's display models (glm's mat4 constructor takes
    // COLUMNS; each initializer list below is read DOWN the matrix):
    //   Transverse: identity           — display (x,y,z) = volume (x,y,z);
    //   Coronal:    rows (x'|y'|z') = (x|z|y) — display x,y = volume x,z;
    //   Sagittal:   rows (x'|y'|z') = (y|z|x) — display x,y = volume y,z.
    switch (axis) {
        case MprAxis::Transverse:
            return place;
        case MprAxis::Coronal:
            return glm::mat4(1.0f, 0.0f, 0.0f, 0.0f, // col0: row0 gets x
                             0.0f, 0.0f, 1.0f, 0.0f, // col1: row2 gets y
                             0.0f, 1.0f, 0.0f, 0.0f, // col2: row1 gets z
                             0.0f, 0.0f, 0.0f, 1.0f) *
                   place;
        case MprAxis::Sagittal:
            return glm::mat4(0.0f, 0.0f, 1.0f, 0.0f, // col0: row2 gets x
                             1.0f, 0.0f, 0.0f, 0.0f, // col1: row0 gets y
                             0.0f, 1.0f, 0.0f, 0.0f, // col2: row1 gets z
                             0.0f, 0.0f, 0.0f, 1.0f) *
                   place;
    }
    // Unreachable (the enum has exactly three values); keep the compiler
    // satisfied on all paths.
    return place;
}

data::Mesh makeBoxMesh(const glm::vec3& min, const glm::vec3& max) {
    // The box as a NON-MANIFOLD quad shell: 6 faces x 4 vertices = 24
    // vertices, 12 triangles. Each face owns its four corner vertices (no
    // vertex is shared between faces), so every vertex is incident to exactly
    // one face's two triangles: MeshGeometry's area-weighted vertex-normal
    // average yields exactly that face's geometric normal on every vertex --
    // each face renders FLAT (the +Z face shades to exactly the base color
    // under the v1 +Z lighting, docs/render.md). Geometrically the shell is
    // the same box as a manifold build (same 8 corners, same 12 triangles),
    // so the FR-app.3 plane/mesh cross-sections are unchanged.
    //
    // The per-face windings are the T11 golden cube's verified outward-CCW
    // triangle pairs (each face's two triangles share the same geometric
    // normal, checked against the T11 winding pattern). The faces are emitted
    // in painter's order for the make3dCamera view (eye along the (1,1,1)
    // diagonal): the far faces (-Z, -X, -Y) first, the near faces (+X, +Y)
    // next, and the +Z face LAST. v1 FBOs are color-only with the depth test
    // off (SPEC §6 / docs/core.md), so the last-drawn face wins at each pixel;
    // this order makes the near +Z face overdraw the far faces at the
    // viewport center, which the FR-app.3 "3D view draws the mesh" gate
    // asserts analytically (see tests/t15_mpr_test.cpp).
    const glm::vec3 c000(min.x, min.y, min.z);
    const glm::vec3 c100(max.x, min.y, min.z);
    const glm::vec3 c110(max.x, max.y, min.z);
    const glm::vec3 c010(min.x, max.y, min.z);
    const glm::vec3 c001(min.x, min.y, max.z);
    const glm::vec3 c101(max.x, min.y, max.z);
    const glm::vec3 c111(max.x, max.y, max.z);
    const glm::vec3 c011(min.x, max.y, max.z);

    // The 8 box corners under the T11 cube's global indices v0..v7:
    //   v0=c000 v1=c100 v2=c110 v3=c010 v4=c001 v5=c101 v6=c111 v7=c011
    // Each face is its 4 corners (local 0..3) plus its 6 local triangle
    // indices (the T11 per-face triangle pairs, outward CCW).
    struct Face {
        std::array<glm::vec3, 4> corners;
        std::array<std::uint32_t, 6> triangles;
    };
    const std::array<Face, 6> faces = {
        Face{{c000, c010, c110, c100},
             {0u, 1u, 2u, 0u, 2u, 3u}}, // front (z = min), -Z
        Face{{c001, c011, c010, c000},
             {0u, 1u, 2u, 0u, 2u, 3u}}, // left  (x = min), -X
        Face{{c000, c100, c101, c001},
             {0u, 1u, 2u, 0u, 2u, 3u}}, // bottom (y = min), -Y
        Face{{c100, c111, c101, c110},
             {0u, 1u, 2u, 0u, 3u, 1u}}, // right (x = max), +X
        Face{{c011, c111, c110, c010},
             {0u, 1u, 2u, 0u, 2u, 3u}}, // top   (y = max), +Y
        Face{{c101, c111, c011, c001},
             {0u, 1u, 2u, 0u, 2u, 3u}}, // back  (z = max), +Z
    };

    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    positions.reserve(24u);
    indices.reserve(36u);
    for (const Face& face : faces) {
        const std::uint32_t base = static_cast<std::uint32_t>(positions.size());
        positions.insert(positions.end(), face.corners.begin(),
                         face.corners.end());
        for (const std::uint32_t i : face.triangles) {
            indices.push_back(base + i);
        }
    }
    return data::Mesh::fromTriangles(std::move(positions), std::move(indices));
}

} // namespace re::app
