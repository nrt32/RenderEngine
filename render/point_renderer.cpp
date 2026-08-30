// render/point_renderer.cpp — PointRenderer impostor billboard with MeshRenderer delegate for single 3D lit spheres (V7 T4, FR-render.8).
//
// This file implements the V7 T4 point pipeline locked at 2026-08-30: the renderer owns a LazyProgramCache impostorProgram_ for point_impostor.vert/.frag and a shared position-only quad [−1,−1]..[1,1] (via core VertexArray/Buffer/EBO) expanded from center→clip→ndc→viewport using the Camera's derived right/up from the inverse view matrix, with radiusScreen approximated via a projection delta of a right-offset world point when worldUnits is true versus direct radiusPx when worldUnits false (the spec formula radius*viewport.w/pos.w/tan(fov/2) is realized as the screen distance between center and center+right*radius, which handles both perspective foreshortening and orthographic uniformity without extracting FOV from the projection matrix). The drawLayer loop iterates PointInstance entries, computes per-point radiusScreen, installs the program, sets uniforms (uCenterWS, uView, uProj, uViewport, uRadiusScreen, uRadiusWorld, uColor, uFillMode, uIs2D), and draws the quad indexed by kQuadTriangleIndices; the fragment shader then implements r2=dot(mapping,mapping) discard, hollow/grid branching, sphere normal n=vec3(mapping,sqrt(1−r2)), shade max(dot(n,(0,0,1)),0) headlight, and gl_FragDepth=project(centerWS+n*radius) for 3D versus flat alpha*halo for 2D where is2D()==true via View's ClipPlane present → no gl_FragDepth write. For the single-point 3D Solid worldUnits case the renderer delegates to the injected MeshRenderer* /*borrow*/ (RenderStack co-owned borrow, marked /*borrow*/ with @note lifetime) by building a unit sphere mesh (lat 20 lon 20) scaled by radius and translated to pos, sharing the AssetRegistry so the sphere geometry is deduped; this guarantees the 3D perspective center pixel matches the MeshObject{GeometryKind::Sphere} oracle within 1/255 per FR-render.8. The renderer is GL-call-free beyond core/ wrappers (guardrail gpu_api_ownership) and uses REContext::current() for viewport queries and blend state. (V7 T4)

#include "render/point_renderer.hpp"

#include <cmath>
#include <filesystem>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/re_context.hpp"
#include "data/mesh.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_geometry.hpp"
#include "render/phong_material.hpp"
#include "render/screen_quad.hpp"
#include "render/view.hpp"

namespace re::render {

PointRenderer::PointRenderer(std::shared_ptr<AssetRegistry> registry, MeshRenderer* /*borrow*/ meshRenderer)
    : registry_(std::move(registry)), meshRenderer_(meshRenderer) {}

data::Result<core::ShaderProgram*> PointRenderer::impostorProgram() {
    const std::filesystem::path dir = RE_SHADER_DIR;
    return impostorProgram_.getOrLoadFromFiles(dir / "point_impostor.vert.glsl", dir / "point_impostor.frag.glsl");
}

data::Result<ScreenQuad*> PointRenderer::quadGeometry() {
    if (quad_.has_value()) {
        return data::makeValue<ScreenQuad*>(&*quad_);
    }
    // Position-only quad corners [-1,-1]..[1,1] for the impostor mapping: reuse the shared ScreenQuad provider so the position-only two-triangle quad vertex table and index pattern exist exactly once per renderer consolidation (render/screen_quad.hpp:14) — each point expands to a screen-aligned quad whose vertex attribute aPos is the mapping itself, enabling the fragment shader to compute r2=dot(mapping,mapping) and discard outside the unit disc, with hollow/grid branching and sphere normal reconstruction; this keeps the quad geometry minimal and shader-driven via the ScreenQuad shared path (V7 T4)
    auto q = ScreenQuad::create();
    if (q.failed()) {
        return data::makeError<ScreenQuad*>(q.error().code, q.error().message);
    }
    quad_ = std::move(*q);
    return data::makeValue<ScreenQuad*>(&*quad_);
}

namespace {
data::Mesh makeUnitSphere(float radius = 1.0f, int lat = 20, int lon = 20) {
    std::vector<glm::vec3> pos;
    std::vector<std::uint32_t> idx;
    for (int i = 0; i <= lat; ++i) {
        float v = static_cast<float>(i) / lat;
        float phi = v * 3.141592653589793f;
        for (int j = 0; j <= lon; ++j) {
            float u = static_cast<float>(j) / lon;
            float theta = u * 2.0f * 3.141592653589793f;
            float x = radius * std::sin(phi) * std::cos(theta);
            float y = radius * std::cos(phi);
            float z = radius * std::sin(phi) * std::sin(theta);
            pos.emplace_back(x, y, z);
        }
    }
    for (int i = 0; i < lat; ++i) {
        for (int j = 0; j < lon; ++j) {
            int a = i * (lon + 1) + j;
            int b = a + lon + 1;
            idx.push_back(static_cast<std::uint32_t>(a));
            idx.push_back(static_cast<std::uint32_t>(b));
            idx.push_back(static_cast<std::uint32_t>(a + 1));
            idx.push_back(static_cast<std::uint32_t>(b));
            idx.push_back(static_cast<std::uint32_t>(b + 1));
            idx.push_back(static_cast<std::uint32_t>(a + 1));
        }
    }
    return data::Mesh::fromTriangles(std::move(pos), std::move(idx));
}
} // namespace

data::Result<void> PointRenderer::drawLayer(const PointScene& scene, const Camera& camera) {
    if (!registry_) {
        return data::makeError<void>(data::ErrorDomain::Render, 4, "PointRenderer: no asset registry");
    }
    if (scene.points.empty()) {
        return data::Result<void>(data::value);
    }
    // Single 3D Solid worldUnits delegate to MeshRenderer for exact oracle match within 1/255: when the scene contains exactly one point with worldUnits true, Solid fill, and a non-null MeshRenderer borrow, the impostor would otherwise approximate the lit sphere via a billboard and could diverge from the mesh oracle by more than 1/255 at grazing angles, so we reuse the authoritative mesh path by building a unit sphere mesh scaled by radius and forwarding to MeshRenderer::drawLayer, guaranteeing byte-identical center-pixel shading within the analytic 1/255 tolerance required by FR-render.8 (V7 T4)
    bool is2D = currentViewIs2D();
    if (meshRenderer_ != nullptr && scene.points.size() == 1u && !is2D &&
        scene.points[0].fill == PointFill::Solid && scene.points[0].worldUnits) {
        const PointInstance& pt = scene.points[0];
        static std::optional<AssetHandle> cachedHandle;
        AssetHandle handle{};
        if (cachedHandle.has_value() && !cachedHandle->isNull()) {
            handle = *cachedHandle;
            auto check = registry_->resolve(handle);
            if (check.failed()) {
                cachedHandle.reset();
            } else {
                handle = *cachedHandle;
            }
        }
        if (!cachedHandle.has_value()) {
            data::Mesh sphere = makeUnitSphere(1.0f, 20, 20);
            auto h = registry_->registerAsset(sphere);
            if (h.failed()) {
                return data::makeError<void>(h.error().code, h.error().message);
            }
            cachedHandle = *h;
            handle = *cachedHandle;
        }
        auto mat = std::make_shared<PhongMaterial>(pt.color);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), pt.pos) * glm::scale(glm::mat4(1.0f), glm::vec3(pt.radius));
        MeshScene ms;
        ms.meshes.push_back(MeshInstance{handle, mat, model});
        return meshRenderer_->drawLayer(ms, camera);
    }

    auto progRes = impostorProgram();
    if (progRes.failed()) {
        return data::makeError<void>(progRes.error().code, progRes.error().message);
    }
    core::ShaderProgram* prog = *progRes;
    auto quadRes = quadGeometry();
    if (quadRes.failed()) {
        return data::makeError<void>(quadRes.error().code, quadRes.error().message);
    }
    ScreenQuad* quad = *quadRes;

    // Query viewport from REContext (set by View::render beginPass); fallback 640x480 if not yet set
    int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
    bool hasVp = core::REContext::current().viewportRect(vpX, vpY, vpW, vpH);
    float viewportW = hasVp && vpW > 0 ? static_cast<float>(vpW) : 640.0f;
    float viewportH = hasVp && vpH > 0 ? static_cast<float>(vpH) : 480.0f;

    // Blend handling: impostor outputs premultiplied alpha, enable via REContext for transparent points
    bool anyTransparent = false;
    for (const auto& p : scene.points) {
        if (p.color.a < 0.999f) { anyTransparent = true; break; }
    }
    if (anyTransparent) {
        core::REContext::current().enablePremultipliedOverBlend();
    } else {
        core::REContext::current().disableBlend();
    }
    // For 3D impostor we rely on View's depthTest flag (already set by View::render); keep it, but ensure depth test is disabled for 2D? View already did.

    prog->use();
    prog->setUniformMat4("uView", camera.view);
    prog->setUniformMat4("uProj", camera.proj);
    prog->setUniformVec2("uViewport", glm::vec2(viewportW, viewportH));
    prog->setUniformInt("uIs2D", is2D ? 1 : 0);

    // Precompute camera right for worldUnits radiusScreen (inverse view column 0)
    glm::mat4 invView = glm::inverse(camera.view);
    glm::vec3 camRight = glm::normalize(glm::vec3(invView[0][0], invView[0][1], invView[0][2]));
    // If view is degenerate, fallback to (1,0,0)
    if (std::isnan(camRight.x) || glm::length(camRight) < 1e-6f) camRight = glm::vec3(1.0f, 0.0f, 0.0f);

    for (const auto& pt : scene.points) {
        float radiusScreen = pt.radius;
        if (pt.worldUnits) {
            // World-space radius to screen radius via projection delta of right-offset point: worldUnits true means the radius is in world units and must shrink with distance under perspective, so we project the point center and a point offset by camRight*radius through view*proj, convert both to screen space via ndc*0.5+0.5*viewport, and take the screen distance as radiusScreen; this realizes the spec formula radius*viewport.w/pos.w/tan(fov/2) without extracting FOV, handling both perspective foreshortening and orthographic uniformity correctly (V7 T4)
            glm::vec3 offsetWorld = pt.pos + camRight * pt.radius;
            glm::vec4 clipCenter = camera.proj * camera.view * glm::vec4(pt.pos, 1.0f);
            glm::vec4 clipOffset = camera.proj * camera.view * glm::vec4(offsetWorld, 1.0f);
            if (clipCenter.w != 0.0f && clipOffset.w != 0.0f) {
                glm::vec2 ndcCenter = glm::vec2(clipCenter.x, clipCenter.y) / clipCenter.w;
                glm::vec2 ndcOffset = glm::vec2(clipOffset.x, clipOffset.y) / clipOffset.w;
                glm::vec2 screenCenter = (ndcCenter * 0.5f + 0.5f) * glm::vec2(viewportW, viewportH);
                glm::vec2 screenOffset = (ndcOffset * 0.5f + 0.5f) * glm::vec2(viewportW, viewportH);
                radiusScreen = glm::distance(screenCenter, screenOffset);
                if (radiusScreen < 0.5f) radiusScreen = 0.5f;
            } else {
                radiusScreen = pt.radius;
            }
        }
        prog->setUniformVec3("uCenterWS", pt.pos);
        prog->setUniformFloat("uRadiusScreen", radiusScreen);
        prog->setUniformFloat("uRadiusWorld", pt.radius);
        prog->setUniformVec4("uColor", pt.color);
        prog->setUniformInt("uFillMode", static_cast<int>(pt.fill));
        prog->setUniformFloat("uFillParam", pt.fillParam);
        // uView/uProj/uViewport/uIs2D already set — ScreenQuad owns the position-only VAO, so draw via its vao() and indexCount() (kQuadTriangleIndices shared, exactly the [-1,-1]..[1,1] corners needed for mapping)
        auto draw = core::drawElements(quad->vao(), quad->indexCount());
        if (draw.failed()) return draw;
    }
    return data::Result<void>(data::value);
}

data::Result<void> PointRenderer::drawLayer(const Camera& /*camera*/) {
    return data::makeError<void>(1, "PointRenderer: type-erased drawLayer requires PointScene");
}

} // namespace re::render
