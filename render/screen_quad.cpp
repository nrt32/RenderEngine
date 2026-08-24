// render/screen_quad.cpp — the ONE definition of the NDC full-screen quad:
// vertex table, index pattern consumption, and GL upload. VolumeRenderer,
// LinkedListOIT, and VolumeSliceRenderer all previously carried byte-identical
// copies of this build sequence (the NDC vertex table alone was defined
// twice); they now each own a ScreenQuad instance built from this single
// implementation.

#include "render/screen_quad.hpp"

namespace re::render {

// Full-screen quad vertices in NDC (x,y): two triangles sharing the diagonal
// corner0→corner2. Attribute layout: 2 floats per vertex (x, y).
constexpr std::array<float, 8> kScreenQuadVerts = {
    -1.0f, -1.0f, // corner 0
    1.0f,  -1.0f, // corner 1
    1.0f,  1.0f,  // corner 2
    -1.0f, 1.0f,  // corner 3
};

data::Result<ScreenQuad> ScreenQuad::create() {
    auto vao = core::VertexArray::create();
    if (vao.failed()) {
        return data::makeError<ScreenQuad>(vao.error().code,
                                           vao.error().message);
    }
    auto vbo = core::VertexBuffer::create();
    if (vbo.failed()) {
        return data::makeError<ScreenQuad>(vbo.error().code,
                                           vbo.error().message);
    }
    auto ebo = core::ElementBuffer::create();
    if (ebo.failed()) {
        return data::makeError<ScreenQuad>(ebo.error().code,
                                           ebo.error().message);
    }

    constexpr std::size_t kStrideBytes = 2u * sizeof(float);

    // The VAO captures both the GL_ARRAY_BUFFER (via setAttribute) and the
    // GL_ELEMENT_ARRAY_BUFFER (via EBO bind) bindings, so both must be bound
    // while the VAO is bound. The buffers outlive this scope inside the
    // returned ScreenQuad because the VAO references them by name.
    vao->bind();
    vbo->bind();
    vbo->upload(kScreenQuadVerts.data(), kScreenQuadVerts.size() * sizeof(float),
                core::BufferUsage::StaticDraw);
    ebo->bind();
    ebo->upload(kQuadTriangleIndices.data(), kQuadTriangleIndices.size(),
                core::BufferUsage::StaticDraw);
    // Interleaved position-only layout: attribute 0 = 2 floats.
    vao->setAttribute(0u, 2, /*normalized=*/false, kStrideBytes, 0u);
    vao->unbind();

    ScreenQuad quad;
    quad.vbo_ = std::move(*vbo);
    quad.ebo_ = std::move(*ebo);
    quad.vao_ = std::move(*vao);
    return data::makeValue<ScreenQuad>(std::move(quad));
}

} // namespace re::render
