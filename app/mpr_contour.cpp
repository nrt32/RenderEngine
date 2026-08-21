// app/mpr_contour.cpp — MPR mesh contour overlay + 3D-view camera scaffolding
// implementation (SPEC §3, FR-app.3).

#include "app/mpr_contour.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <utility>

namespace re::app {

namespace {

/// Convert a straight RGBA color in [0, 1] to RGBA8 bytes (round-half-up,
/// matching the render/ convention: byte = round(c * 255 + 0.5)).
std::uint8_t toByte(float v) noexcept {
    return static_cast<std::uint8_t>(std::round(v * 255.0f));
}

/// The sign of `v` (-1, 0 or +1), used for triangle-plane edge classification.
int signOf(float v) noexcept {
    return (v > 0.0f) ? 1 : ((v < 0.0f) ? -1 : 0);
}

/// The FR-app.3 contour band: pixels within 2 px (Euclidean) of the curve must
/// match the contour color. The rasterizer colors pixels within
/// `kContourBandPx` of a segment; the 1e-3 guard covers float rounding at the
/// exact 2 px boundary. The gate measures only pixels within 2 px of the
/// analytic curve, so the guard never widens the measured band — it only
/// guarantees every such pixel is colored.
constexpr float kContourBandPx = 2.0f + 1e-3f;

/// Squared Euclidean distance from `p` to the segment [a, b] (closed-form
/// point-segment distance; the projected point is clamped to the segment).
float pointSegmentDistanceSq(const glm::vec2& p, const glm::vec2& a,
                             const glm::vec2& b) noexcept {
    const glm::vec2 ab = b - a;
    const float lenSq = glm::dot(ab, ab);
    if (lenSq <= 0.0f) {
        const glm::vec2 pa = p - a;
        return glm::dot(pa, pa);
    }
    const float t = glm::clamp(glm::dot(p - a, ab) / lenSq, 0.0f, 1.0f);
    const glm::vec2 q = a + t * ab;
    const glm::vec2 pq = p - q;
    return glm::dot(pq, pq);
}

/// The 3D view's vertical field of view: 45 degrees in radians.
constexpr float k3dFovY = 0.7853981633974483f;

/// The camera framing factor: the eye stands this many bounding diagonals from
/// the slice-state crosshair (documented in make3dCamera).
constexpr float kCameraDistanceFactor = 1.5f;

} // namespace

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

std::vector<ContourSegment> meshPlaneContour(const data::Mesh& mesh,
                                             const SlicePlane& plane) {
    // Map the MPR axis to the held coordinate index (0=x, 1=y, 2=z) and the
    // two free axes that form the slice image's (x, y) pixel space.
    int held = 2;
    int freeX = 0;
    int freeY = 1;
    switch (plane.axis) {
        case MprAxis::Transverse:
            held = 2;
            freeX = 0;
            freeY = 1;
            break;
        case MprAxis::Coronal:
            held = 1;
            freeX = 0;
            freeY = 2;
            break;
        case MprAxis::Sagittal:
            held = 0;
            freeX = 1;
            freeY = 2;
            break;
    }

    std::vector<ContourSegment> segments;
    const std::vector<glm::vec3>& positions = mesh.positions();
    const std::vector<std::uint32_t>& indices = mesh.indices();
    const std::size_t triangleCount = mesh.triangleCount();

    for (std::size_t t = 0u; t < triangleCount; ++t) {
        const glm::vec3 p[3] = {positions[indices[3u * t]],
                                positions[indices[3u * t + 1u]],
                                positions[indices[3u * t + 2u]]};
        // Signed distances of the three vertices to the plane (the plane's
        // normal is the held axis' unit vector).
        const float d[3] = {p[0][held] - plane.coordinate,
                            p[1][held] - plane.coordinate,
                            p[2][held] - plane.coordinate};

        // Count the strictly-positive and strictly-negative vertices: a
        // triangle straddles the plane only when both counts are non-zero.
        int positive = 0;
        int negative = 0;
        for (const float v : d) {
            if (v > 0.0f) {
                ++positive;
            } else if (v < 0.0f) {
                ++negative;
            }
        }
        if (positive == 0 || negative == 0) {
            continue; // fully on one side, tangent, or coplanar: no segment
        }

        // Collect the crossing points: the intersection of the plane with each
        // edge whose endpoints lie on opposite sides (a vertex exactly on the
        // plane crosses with any strictly-sided vertex, at t = 0).
        std::vector<glm::vec3> crossing;
        const int edges[3][2] = {{0, 1}, {1, 2}, {2, 0}};
        for (const auto& edge : edges) {
            const float da = d[edge[0]];
            const float db = d[edge[1]];
            if (signOf(da) != signOf(db)) {
                const float t = da / (da - db); // da - db != 0: signs differ
                crossing.push_back(p[edge[0]] + t * (p[edge[1]] - p[edge[0]]));
            }
        }

        // Deduplicate near-coincident crossing points (a plane through a
        // vertex yields coincident points at that vertex).
        std::vector<glm::vec3> distinct;
        for (const glm::vec3& c : crossing) {
            bool duplicate = false;
            for (const glm::vec3& known : distinct) {
                if (glm::length(c - known) < 1e-6f) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                distinct.push_back(c);
            }
        }
        if (distinct.size() < 2u) {
            continue; // tangent (single crossing point): no segment
        }

        // Project the two crossing points onto the view's two free axes.
        segments.push_back(
            ContourSegment{glm::vec2(distinct[0][freeX], distinct[0][freeY]),
                           glm::vec2(distinct[1][freeX], distinct[1][freeY])});
    }
    return segments;
}

data::Image overlayContour(const data::Image& image,
                           const std::vector<ContourSegment>& curve,
                           const volume::RgbaColor& color) {
    const std::uint8_t r = toByte(color.r);
    const std::uint8_t g = toByte(color.g);
    const std::uint8_t b = toByte(color.b);
    const std::uint8_t a = toByte(color.a);
    const float bandSq = kContourBandPx * kContourBandPx;

    std::vector<std::uint8_t> pixels = image.pixels();
    for (std::int32_t py = 0; py < image.height(); ++py) {
        for (std::int32_t px = 0; px < image.width(); ++px) {
            const glm::vec2 center(static_cast<float>(px) + 0.5f,
                                   static_cast<float>(py) + 0.5f);
            float minDistSq = std::numeric_limits<float>::infinity();
            for (const ContourSegment& seg : curve) {
                minDistSq = std::min(
                    minDistSq, pointSegmentDistanceSq(center, seg[0], seg[1]));
            }
            if (minDistSq <= bandSq) {
                const std::size_t offset =
                    (static_cast<std::size_t>(py) *
                         static_cast<std::size_t>(image.width()) +
                     static_cast<std::size_t>(px)) *
                    static_cast<std::size_t>(image.channels());
                pixels[offset] = r;
                pixels[offset + 1u] = g;
                pixels[offset + 2u] = b;
                pixels[offset + 3u] = a;
            }
        }
    }
    return data::Image(image.width(), image.height(), image.channels(),
                       std::move(pixels));
}

render::Camera make3dCamera(const MprSliceState& state,
                            const data::Aabb& meshBounds, float aspect) {
    // The slice-state crosshair: the intersection point of the three slice
    // planes, in voxel-index units through the voxel centers.
    const glm::vec3 crosshair(static_cast<float>(state.sagittalX) + 0.5f,
                              static_cast<float>(state.coronalY) + 0.5f,
                              static_cast<float>(state.transverseZ) + 0.5f);

    // The camera stands `kCameraDistanceFactor` bounding diagonals from the
    // crosshair along the (1,1,1) diagonal direction and looks at it, so the
    // box (and the slice planes' intersection) stays framed as the slice state
    // changes.
    const float diagonal = glm::length(meshBounds.max - meshBounds.min);
    const float distance = kCameraDistanceFactor * diagonal;
    const glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    const glm::vec3 eye = crosshair + direction * distance;

    render::Camera camera;
    camera.position = eye;
    camera.view = glm::lookAt(eye, crosshair, glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj =
        glm::perspective(k3dFovY, aspect, distance / 10.0f, distance * 10.0f);
    return camera;
}

} // namespace re::app