// tests/test_helpers.cpp — single-source test helpers (T6 IT2 — the cpp that
// provides the single definition for the golden quad mesh, default camera, window
// framebuffer pair, and pixel helpers; the file exists so the gate can prove a
// single source via grep count 1 for the quad-maker (see header), and the ~150
// lines removed from ten test files eliminates drift while keeping the monolithic
// binary and tN_ naming as intentional single-context gate choices documented in
// nfr.md).

#include "tests/test_helpers.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#include "test_utils/pixel_reader.hpp"

namespace re::tests {

data::Mesh makeQuadMesh() {
    std::vector<glm::vec3> positions = {
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
    };
    std::vector<std::uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};
    return data::Mesh::fromTriangles(std::move(positions), std::move(indices));
}

render::Camera makeCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

WindowTarget::WindowTarget(core::Texture2D c, core::Framebuffer f)
    : color(std::move(c)), framebuffer(std::move(f)) {}

WindowTarget makeWindow() { return makeWindow(1280, 480); }

WindowTarget makeWindow(int width, int height) {
    auto color = core::Texture2D::create();
    auto framebuffer = core::Framebuffer::create();
    EXPECT_TRUE(color.ok()) << color.error().message;
    EXPECT_TRUE(framebuffer.ok()) << framebuffer.error().message;
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(width) * height * 4u, 0u);
    color->bind(0u);
    color->upload(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), zeros.data());
    color->unbind(0u);
    framebuffer->bind();
    framebuffer->attachColor(*color);
    EXPECT_TRUE(framebuffer->isComplete());
    framebuffer->unbind();
    return WindowTarget(std::move(*color), std::move(*framebuffer));
}

std::vector<std::uint8_t> readPixel(core::Framebuffer& framebuffer,
                                    std::uint32_t x, std::uint32_t y) {
    framebuffer.bind();
    std::vector<std::uint8_t> pixels;
    re::test_utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    framebuffer.unbind();
    return pixels;
}

std::vector<std::uint8_t> readPixel(std::uint32_t x, std::uint32_t y) {
    std::vector<std::uint8_t> pixels;
    re::test_utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    return pixels;
}

void expectPixel(const std::vector<std::uint8_t>& pixel, int r, int g, int b,
                 int a, const char* where, int tolerance) {
    EXPECT_NEAR(pixel[0], r, tolerance) << "R at " << where;
    EXPECT_NEAR(pixel[1], g, tolerance) << "G at " << where;
    EXPECT_NEAR(pixel[2], b, tolerance) << "B at " << where;
    EXPECT_NEAR(pixel[3], a, tolerance) << "A at " << where;
}

void expectPixel(const std::vector<std::uint8_t>& pixel, int r, int g, int b,
                 const char* where, int tolerance) {
    expectPixel(pixel, r, g, b, 255, where, tolerance);
}

} // namespace re::tests
