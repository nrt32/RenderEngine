// app/mpr_sample.cpp — Multi-Planar Reconstruction (MPR) sample (T14, FR-app.2).
//
// Demonstrates the MPR capability (SPEC §1 goal 6): a single 1280x960 window
// with a 2x2 viewport grid (four 640x480 viewports; T top-left, C top-right,
// S bottom-left, 3D bottom-right, per SPEC FR-app.2). The Transverse (constant
// Z), Coronal (constant Y) and Sagittal (constant X) views render the volume
// slice along their pinned axis (SPEC §4 FR-app.2); the 3D view is reserved
// for T15 (mesh + contour) and shows a background in T14.
//
// Rendering architecture (app-level composition, SPEC §3):
//   - the 2x2 viewport layout and the per-axis slice sampling come from the
//     shared app/mpr_slice scaffolding (mprViewports / makeSliceImage), which
//     the T14 gate tests headlessly;
//   - each of the three slice views is rendered into its own 640x480 offscreen
//     FBO via render::PlaneRenderer: a per-view orthographic camera maps the
//     slice image's pixel space [0,imgW]x[0,imgH] onto the full viewport, and
//     the shared unit quad is scaled onto that pixel rectangle (so the whole
//     slice fills the view);
//   - a small present pass (core/ wrappers only) composites the FBOs onto the
//     window's default framebuffer in their viewport regions.
//
// The sample exits cleanly (code 0) after RE_SAMPLE_MAX_FRAMES frames (default
// 300) so the gate can run it headlessly under Xvfb within a timeout
// (FR-app.1: exit code 0, no sanitizer reports).

#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/mpr_slice.hpp"
#include "app/sample_harness.hpp"
#include "core/draw.hpp"
#include "core/element_buffer.hpp"
#include "core/framebuffer.hpp"
#include "core/shader_program.hpp"
#include "core/texture2d.hpp"
#include "core/vertex_array.hpp"
#include "core/vertex_buffer.hpp"
#include "core/window.hpp"
#include "data/image.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "render/mesh_renderer.hpp" // render::Camera / render::RenderTarget
#include "render/plane_renderer.hpp"
#include "volume/transfer_function.hpp"

#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

namespace {

// Convenience namespace aliases for the sample's own .cpp (NAMING_CONVENTIONS
// §7: `using`/aliases are allowed inside a .cpp translation unit).
namespace core = re::core;
namespace data = re::data;
namespace volume = re::volume;
namespace render = re::render;
namespace app = re::app;

// The harness window size (SPEC FR-app.2: 1280x960).
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 960;
// Default number of frames before the sample exits cleanly.
constexpr int kDefaultFrames = 300;
// The offscreen slice-view resolution (SPEC FR-app.2: each viewport is 640x480).
constexpr std::uint32_t kViewportWidth = 640u;
constexpr std::uint32_t kViewportHeight = 480u;

/// A CT window/level transfer function over the sample_ct value range
/// ([-3024, 2529], SPEC §7): air (low) transparent, soft tissue opaque/bright.
/// Deterministic control points (FR-vol.1); monotonic alpha ramp. Mirrors the
/// T12 volume sample's TF.
volume::TransferFunction makeCtTransferFunction() {
    using CP = volume::TransferFunction::ControlPoint;
    return volume::TransferFunction(
        {CP{-1024.0f, volume::RgbaColor{0.0f, 0.0f, 0.0f, 0.0f}},
         CP{-300.0f, volume::RgbaColor{0.05f, 0.05f, 0.10f, 0.05f}},
         CP{40.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 0.90f}},
         CP{300.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 1.00f}},
         CP{2500.0f, volume::RgbaColor{1.00f, 1.00f, 1.00f, 1.00f}}});
}

/// Build the shared slice-state: hold each 2D view on the middle slice of its
/// axis (shared slice-state/camera scaffolding, T14). T15 adds camera control
/// that drives these.
app::MprSliceState makeInitialSliceState(const data::VolumeDataset& dataset) {
    app::MprSliceState state;
    state.transverseZ = dataset.sizeZ() / 2u;
    state.coronalY = dataset.sizeY() / 2u;
    state.sagittalX = dataset.sizeX() / 2u;
    return state;
}

/// The orthographic camera for one 2D slice view (the shared 2D-view camera
/// configuration, derived per view from that view's slice image): maps the
/// image's pixel space [0,imgW]x[0,imgH] onto the full 640x480 viewport. The
/// camera looks down -Z from +Z at the quad (which sits at z = 0), so the
/// slice displays with the image's top-left at the viewport's top-left (the
/// PlaneRenderer orientation convention, FR-render.5 / render/plane_renderer.hpp).
render::Camera makeSliceCamera(const data::Image& image) {
    render::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera.view =
        glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj =
        glm::ortho(0.0f, static_cast<float>(image.width()), 0.0f,
                   static_cast<float>(image.height()), 0.1f, 10.0f);
    return camera;
}

/// The model matrix scaling the shared unit quad [-1,1]^2 onto the slice
/// image's pixel rectangle [0,imgW]x[0,imgH], so the whole slice image fills
/// the viewport (with the shared camera above).
glm::mat4 makeSliceModel(const data::Image& image) {
    const float halfW = static_cast<float>(image.width()) * 0.5f;
    const float halfH = static_cast<float>(image.height()) * 0.5f;
    return glm::translate(glm::mat4(1.0f), glm::vec3(halfW, halfH, 0.0f)) *
           glm::scale(glm::mat4(1.0f), glm::vec3(halfW, halfH, 1.0f));
}

/// Present shaders (GLSL 450, SPEC §8): draw a fullscreen textured quad in the
/// current viewport, sampling an RGBA8 texture 1:1.
constexpr char kPresentVertexShader[] =
    "#version 450 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aUV;\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "    vUV = aUV;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

constexpr char kPresentFragmentShader[] =
    "#version 450 core\n"
    "in vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "layout(location = 0) out vec4 oColor;\n"
    "void main() {\n"
    "    oColor = texture(uTex, vUV);\n"
    "}\n";

/// A view FBO: its color attachment texture + the framebuffer that renders to
/// it. The color texture is needed for the present pass (sampled as a quad).
struct ViewFramebuffer {
    core::Texture2D color;
    core::Framebuffer framebuffer;
    ViewFramebuffer(core::Texture2D c, core::Framebuffer f)
        : color(std::move(c)), framebuffer(std::move(f)) {}
};

/// Create a 640x480 color-only offscreen FBO for one slice view.
data::Result<ViewFramebuffer> makeViewFramebuffer() {
    auto color = core::Texture2D::create();
    if (color.failed()) {
        return data::makeError<ViewFramebuffer>(color.error().code,
                                                color.error().message);
    }
    auto framebuffer = core::Framebuffer::create();
    if (framebuffer.failed()) {
        return data::makeError<ViewFramebuffer>(framebuffer.error().code,
                                                framebuffer.error().message);
    }
    std::vector<std::uint8_t> zeros(
        static_cast<std::size_t>(kViewportWidth) * kViewportHeight * 4u, 0u);
    color->bind(0u);
    color->upload(kViewportWidth, kViewportHeight, zeros.data());
    color->unbind(0u);
    framebuffer->bind();
    framebuffer->attachColor(*color);
    if (!framebuffer->isComplete()) {
        framebuffer->unbind();
        return data::makeError<ViewFramebuffer>(
            1, "MPR: slice-view framebuffer incomplete");
    }
    framebuffer->unbind();
    return data::makeValue<ViewFramebuffer>(
        ViewFramebuffer(std::move(*color), std::move(*framebuffer)));
}

/// The MPR sample: owns the volume + transfer function + the slice-view
/// scaffold and renders one frame of the 2x2 grid into the window's default
/// framebuffer.
class MPRView final : public app::ISample {
   public:
    MPRView(data::VolumeDataset dataset, volume::TransferFunction tf)
        : dataset_(std::move(dataset)),
          tf_(std::move(tf)),
          sliceState_(makeInitialSliceState(dataset_)),
          transverseImage_(makeSliceImage(dataset_, tf_, app::MprAxis::Transverse,
                                          sliceState_.transverseZ)),
          coronalImage_(makeSliceImage(dataset_, tf_, app::MprAxis::Coronal,
                                       sliceState_.coronalY)),
          sagittalImage_(makeSliceImage(dataset_, tf_, app::MprAxis::Sagittal,
                                        sliceState_.sagittalX)) {
        // One shared unit quad, scaled per slice view onto that view's image
        // pixel rectangle (makeSliceModel) and viewed through a per-view
        // orthographic camera (makeSliceCamera) — the shared 2D-view camera
        // scaffolding that maps each slice image onto its full viewport.
        quad_ = render::PlaneGeometry::unitQuadXY();
    }

    data::Result<void> renderFrame(int width, int height) override {
        auto ensure = ensureTargets();
        if (ensure.failed()) {
            return ensure;
        }
        auto slices = renderSlices();
        if (slices.failed()) {
            return slices;
        }
        return present(width, height);
    }

    const char* title() const override {
        return "MPR sample: 2x2 viewport grid (T/C/S slices + 3D)";
    }

    const char* instructions() const noexcept override {
        return "Capability: Multi-Planar Reconstruction (SPEC FR-app.2).\n"
               "A single 1280x960 window shows four 640x480 viewports in a "
               "2x2 grid:\n"
               "T (top-left) = Transverse slice (constant Z),\n"
               "C (top-right) = Coronal slice (constant Y),\n"
               "S (bottom-left) = Sagittal slice (constant X),\n"
               "3D (bottom-right) = reserved for the 3D view (T15).\n"
               "Run the sample, then close the window (or set "
               "RE_SAMPLE_MAX_FRAMES) to exit.";
    }

   private:
    /// Create the four 640x480 view FBOs and the present-pass GL resources,
    /// once. Returns a typed error on any GL failure.
    data::Result<void> ensureTargets() {
        if (targetsReady_) {
            return data::Result<void>(data::value);
        }
        for (std::size_t i = 0u; i < viewFramebuffers_.size(); ++i) {
            auto fb = makeViewFramebuffer();
            if (fb.failed()) {
                return data::makeError<void>(fb.error().code,
                                             fb.error().message);
            }
            viewFramebuffers_[i] = std::move(*fb);
        }

        // Present pass: a fullscreen textured quad.
        auto program = core::ShaderProgram::create(kPresentVertexShader,
                                                   kPresentFragmentShader);
        if (program.failed()) {
            return data::makeError<void>(program.error().code,
                                         program.error().message);
        }
        presentProgram_ = std::move(*program);

        auto vao = core::VertexArray::create();
        auto vbo = core::VertexBuffer::create();
        auto ebo = core::ElementBuffer::create();
        if (vao.failed() || vbo.failed() || ebo.failed()) {
            return data::makeError<void>(
                1, "MPR: present-pass buffer creation failed");
        }
        const std::vector<float> verts = {
            -1.0f, -1.0f, 0.0f, 0.0f, // bottom-left
            1.0f,  -1.0f, 1.0f, 0.0f, // bottom-right
            1.0f,  1.0f,  1.0f, 1.0f, // top-right
            -1.0f, 1.0f,  0.0f, 1.0f, // top-left
        };
        const std::vector<std::uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};
        vao->bind();
        vbo->bind();
        vbo->upload(verts.data(), verts.size() * sizeof(float),
                    core::BufferUsage::StaticDraw);
        ebo->bind();
        ebo->upload(indices.data(), indices.size(),
                    core::BufferUsage::StaticDraw);
        vao->setAttribute(0u, 2, /*normalized=*/false, 4u * sizeof(float), 0u);
        vao->setAttribute(1u, 2, /*normalized=*/false, 4u * sizeof(float),
                          2u * sizeof(float));
        vao->unbind();
        presentVao_ = std::move(*vao);
        presentVbo_ = std::move(*vbo);
        presentEbo_ = std::move(*ebo);
        presentIndexCount_ = indices.size();

        targetsReady_ = true;
        return data::Result<void>(data::value);
    }

    /// Render the three slice views (T/C/S) into their own 640x480 FBOs via
    /// PlaneRenderer; the 3D view FBO is left cleared (T15 fills it).
    data::Result<void> renderSlices() {
        const std::array<const data::Image*, 3> sliceImages = {
            &transverseImage_, &coronalImage_, &sagittalImage_};

        for (std::size_t i = 0u; i < 3u; ++i) {
            const data::Image* image = sliceImages[i];
            // Per-view camera + model (the shared 2D-view camera scaffolding):
            // the ortho maps the image's pixel space onto the viewport and the
            // quad is scaled onto the image's pixel rectangle, so the whole
            // slice fills the 640x480 view.
            const render::Camera viewCamera = makeSliceCamera(*image);
            const glm::mat4 viewModel = makeSliceModel(*image);
            render::PlaneScene scene;
            scene.planes.push_back(
                render::PlaneInstance{&quad_, image, viewModel});
            render::RenderTarget target;
            target.framebuffer = &viewFramebuffers_[i]->framebuffer;
            target.width = kViewportWidth;
            target.height = kViewportHeight;
            target.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            auto result = sliceRenderer_.render(scene, viewCamera, target);
            if (result.failed()) {
                return result;
            }
        }

        // Clear the 3D view FBO to a distinct background (T15 renders the mesh
        // + contour here).
        viewFramebuffers_[3]->framebuffer.bind();
        core::setViewport(0, 0, static_cast<int>(kViewportWidth),
                          static_cast<int>(kViewportHeight));
        core::setClearColor(0.10f, 0.10f, 0.14f, 1.0f);
        core::clearColor();
        viewFramebuffers_[3]->framebuffer.unbind();
        return data::Result<void>(data::value);
    }

    /// Composite the four view FBOs onto the window's default framebuffer in
    /// their viewport regions (SPEC FR-app.2 grid). The 3D viewport is drawn
    /// from its (background) FBO; T15 fills it with the mesh.
    data::Result<void> present(int width, int height) {
        const std::array<app::MprViewport, 4> views = app::mprViewports(width, height);
        core::bindDefaultFramebuffer();
        core::setViewport(0, 0, width, height);
        core::setClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        core::clearColor();
        core::disableDepthTest();
        core::disableBlend();

        presentProgram_->use();
        presentProgram_->setUniformInt("uTex", 0);

        for (std::size_t i = 0u; i < views.size(); ++i) {
            const app::MprViewport& vp = views[i];
            core::setViewport(vp.x, vp.y, vp.width, vp.height);
            viewFramebuffers_[i]->color.bind(0u);
            auto draw = core::drawElements(*presentVao_, presentIndexCount_);
            if (draw.failed()) {
                return draw;
            }
            viewFramebuffers_[i]->color.unbind(0u);
        }
        return data::Result<void>(data::value);
    }

    data::VolumeDataset dataset_;
    volume::TransferFunction tf_;
    app::MprSliceState sliceState_;

    data::Image transverseImage_;
    data::Image coronalImage_;
    data::Image sagittalImage_;

    render::PlaneGeometry quad_;
    render::PlaneRenderer sliceRenderer_;

    bool targetsReady_{false};
    std::array<std::optional<ViewFramebuffer>, 4> viewFramebuffers_;

    std::optional<core::ShaderProgram> presentProgram_;
    std::optional<core::VertexArray> presentVao_;
    std::optional<core::VertexBuffer> presentVbo_;
    std::optional<core::ElementBuffer> presentEbo_;
    std::size_t presentIndexCount_{0u};
};

} // namespace

int main() {
    const std::string volumePath =
        std::string(RE_SOURCE_DIR) + "/data/volumes/sample_ct.nrrd";
    auto volumeResult = re::io::loadNrrdVolume(volumePath);
    if (volumeResult.failed()) {
        spdlog::error("mpr sample: failed to load '{}': {}", volumePath,
                      volumeResult.error().message);
        return 1;
    }

    auto windowResult =
        re::core::Window::create(kWindowWidth, kWindowHeight,
                                 "RenderEngine - MPR Sample");
    if (windowResult.failed()) {
        spdlog::error("mpr sample: {}", windowResult.error().message);
        return 1;
    }

    auto sample = std::make_unique<MPRView>(std::move(*volumeResult),
                                            makeCtTransferFunction());
    re::app::SampleHarness harness(std::move(*windowResult), std::move(sample));
    return harness.run(re::app::sampleMaxFrames(kDefaultFrames));
}
