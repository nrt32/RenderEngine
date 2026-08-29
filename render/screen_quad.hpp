#pragma once

// render/screen_quad.hpp — ONE internal provider for the NDC full-screen quad
// (renderer-consolidation deliverable: the position-only two-triangle quad
// VAO used to be built identically by VolumeRenderer, LinkedListOIT, and
// VolumeSliceRenderer, with the NDC vertex table defined twice; the vertex
// table, index pattern, and upload code now exist exactly once, here).
//
// Scope: this is the FULL-SCREEN NDC quad for techniques that shade every
// pixel (ray-cast volume, slice extraction, OIT composite). PlaneRenderer's
// unit quad is a DIFFERENT geometry — an interleaved position+UV+normal
// layout built from PlaneGeometry::unitQuadXY, because its shader samples a
// texture per-vertex — so it keeps its own builder but draws with the same
// shared triangle index pattern (`kQuadTriangleIndices` below).
//
// render/ is GL-call-free: buffers are uploaded through core/ RAII objects
// and drawn via core::drawElements (guardrail gpu_api_ownership).

#include <array>
#include <cstdint>
#include <optional>

#include "core/element_buffer.hpp"
#include "core/vertex_array.hpp"
#include "core/vertex_buffer.hpp"
#include "data/result.hpp"

namespace re::render {

/// The one quad index pattern: corner order 0..3 covered by the two
/// counter-clockwise triangles (0,1,2) and (0,2,3). Header-placed because
/// both this provider and PlaneRenderer's interleaved unit-quad builder need
/// it (constants live in .cpp files unless ≥2 translation units require them).
inline constexpr std::array<std::uint32_t, 6> kQuadTriangleIndices = {0u, 1u,
                                                                      2u, 0u,
                                                                      2u, 3u};

/// A renderer-owned instance of the NDC full-screen quad: corners
/// (-1,-1), (1,-1), (1,1), (-1,1) at z=0 in NDC, interleaved as 2 floats per
/// vertex (attribute 0), indexed by kQuadTriangleIndices. Create once per
/// renderer via create(); issue draws with core::drawElements(vao(),
/// indexCount()).
class ScreenQuad {
   public:
    /// Upload the quad into fresh GL buffers and capture them in a VAO.
    /// Returns an error if no GL context is current or a buffer cannot be
    /// created.
    static data::Result<ScreenQuad> create();

    ScreenQuad() noexcept = default;

    ScreenQuad(const ScreenQuad&) = delete;
    ScreenQuad& operator=(const ScreenQuad&) = delete;

    ScreenQuad(ScreenQuad&& other) noexcept = default;
    ScreenQuad& operator=(ScreenQuad&& other) noexcept = default;

    /// The captured vertex array (attribute 0 = 2-float NDC position).
    /// @note lifetime: non-owning view of this object's own storage — valid
    /// while this ScreenQuad lives.
    core::VertexArray& vao() noexcept { return *vao_; }

    /// Index count of one full-screen draw (always 6: two triangles).
    std::size_t indexCount() const noexcept { return kQuadTriangleIndices.size(); }

   private:
    std::optional<core::VertexArray> vao_;
    // The VAO captures the array buffer (via setAttribute) and the
    // element array buffer (via EBO bind) bindings by name, so both
    // buffers must outlive it — they are kept as siblings here.
    std::optional<core::VertexBuffer> vbo_;
    std::optional<core::ElementBuffer> ebo_;
};

} // namespace re::render
