// broker/csg_object_mapper.cpp — CsgObjectMapper cached translation (no raw gl*).
//
// The mapper translates scene::CsgObject's flat multi-subtract/multi-paint intent into RE-minimal handles via AssetRegistry (dedup by ContentHash), preserving per-operand operandTransform and paint metadata for the GPU Puxel stage. Base handle plus subtractor/paint handle vectors and their transforms/blends/interior flags are the RE currency (never raw mesh bytes — asset_indirection, RE-minimal). World bounds are derived as model * localBounds of the base mesh (AABB transformed). Cache key includes generation plus Σ operandHashes over hash(contentHash+operandTransform+matHash+paintBlend+paintInterior) so a pure transform bump reuses handles while operand content changes miss (per-field generation + contentHash cache helpers via IDirtyTracker + GenerationTracker, SPEC §10.4). (V7 T6)

#include "broker/csg_object_mapper.hpp"

#include <cstdint>
#include <cstring>
#include <limits>

#include "data/content_hash.hpp"
#include "data/mesh.hpp"

namespace re::broker {

namespace {

inline uint64_t hashCombine(uint64_t a, uint64_t b) noexcept {
    return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
}

inline uint64_t hashFloat(float v) noexcept {
    uint32_t bits = 0u;
    static_assert(sizeof(float) == sizeof(uint32_t), "float 32");
    std::memcpy(&bits, &v, sizeof(float));
    if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0u) bits = 0x7fc00000u;
    // widen to 64 via LE canonical
    uint32_t le = re::data::toLE32(bits);
    return static_cast<uint64_t>(le) * 0x9e3779b97ULL + 0x85ebca6bULL;
}

uint64_t hashMat4(const glm::mat4& m) noexcept {
    uint64_t h = 1469598103934665603ULL;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            h = hashCombine(h, hashFloat(m[c][r]));
        }
    }
    return h;
}

uint64_t hashPhong(const scene::PhongDesc& p) noexcept {
    uint64_t h = 1469598103934665603ULL;
    h = hashCombine(h, hashFloat(p.baseColor.r));
    h = hashCombine(h, hashFloat(p.baseColor.g));
    h = hashCombine(h, hashFloat(p.baseColor.b));
    h = hashCombine(h, hashFloat(p.baseColor.a));
    h = hashCombine(h, hashFloat(p.specular.r));
    h = hashCombine(h, hashFloat(p.specular.g));
    h = hashCombine(h, hashFloat(p.specular.b));
    h = hashCombine(h, hashFloat(p.shininess));
    h = hashCombine(h, p.doubleSided ? 1u : 0u);
    return h;
}

uint64_t hashMeshMaterial(const scene::MeshMaterialDesc& m) noexcept {
    return hashPhong(m.phong);
}

re::data::Aabb computeLocalAabb(const re::data::Mesh& mesh) noexcept {
    re::data::Aabb aabb;
    if (mesh.positions().empty()) return aabb;
    glm::vec3 mn = mesh.positions()[0];
    glm::vec3 mx = mn;
    for (auto& p : mesh.positions()) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    aabb.min = mn;
    aabb.max = mx;
    return aabb;
}

re::data::Aabb transformAabb(const re::data::Aabb& box, const glm::mat4& m) noexcept {
    // Transform 8 corners, recompute min/max
    glm::vec3 corners[8] = {
        {box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z},
        {box.min.x, box.max.y, box.min.z}, {box.max.x, box.max.y, box.min.z},
        {box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z},
        {box.min.x, box.max.y, box.max.z}, {box.max.x, box.max.y, box.max.z},
    };
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (auto& c : corners) {
        glm::vec4 t = m * glm::vec4(c, 1.0f);
        glm::vec3 p(t.x / t.w, t.y / t.w, t.z / t.w);
        // For affine (no perspective) w==1, but keep general
        if (t.w != 0.0f) p = glm::vec3(t.x, t.y, t.z) / t.w;
        else p = glm::vec3(t.x, t.y, t.z);
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    return re::data::Aabb{mn, mx};
}

} // namespace

uint64_t CsgObjectMapper::computeOperandHash(const scene::CsgObject& app) const noexcept {
    // Σ hash(AssetId+gen+contentHash+operandTransform+matHash+paintBlend+paintInterior)
    // For this iteration AssetId is the contentHash identity (hashed at load/register time, never per frame), gen is the object's generation (per-field), plus per-operand contentHash, transform, matHash, and for paints blend+interior.
    uint64_t acc = 1469598103934665603ULL;
    // Base operand
    if (app.base.mesh) {
        uint64_t ch = re::data::computeContentHash(*app.base.mesh);
        acc = hashCombine(acc, ch);
        acc = hashCombine(acc, static_cast<uint64_t>(app.generation));
        acc = hashCombine(acc, hashMat4(app.base.operandTransform));
        acc = hashCombine(acc, hashMeshMaterial(app.base.material));
    } else {
        acc = hashCombine(acc, 0xdeadbeefULL);
    }
    // Subtractors
    for (auto& op : app.subtractors) {
        uint64_t ch = op.mesh ? re::data::computeContentHash(*op.mesh) : 0ULL;
        uint64_t h = hashCombine(ch, hashMat4(op.operandTransform));
        h = hashCombine(h, hashMeshMaterial(op.material));
        // mix with generation to capture per-operand gen bump (object gen covers, but also mesh content)
        h = hashCombine(h, static_cast<uint64_t>(app.generation));
        acc = hashCombine(acc, h);
    }
    // Paints: blend + interior
    for (auto& p : app.paints) {
        uint64_t ch = p.oper.mesh ? re::data::computeContentHash(*p.oper.mesh) : 0ULL;
        uint64_t h = hashCombine(ch, hashMat4(p.oper.operandTransform));
        h = hashCombine(h, hashMeshMaterial(p.oper.material));
        h = hashCombine(h, hashFloat(p.blend));
        h = hashCombine(h, p.paintInterior ? 1ULL : 0ULL);
        h = hashCombine(h, static_cast<uint64_t>(app.generation));
        acc = hashCombine(acc, h);
    }
    return acc;
}

bool CsgObjectMapper::isCacheHit(const AppType& app, const scene::TranslateContext& ctx,
                                 const Entry& e) const {
    (void)ctx;
    if (e.generation != app.generation) return false;
    auto it = operandHashCache_.find(app.id);
    if (it == operandHashCache_.end()) return false;
    uint64_t cur = computeOperandHash(app);
    return it->second == cur;
}

void CsgObjectMapper::fillEntry(Entry& e, const AppType& app, const scene::TranslateContext& ctx,
                                const ReType& instance) const {
    (void)ctx;
    e.generation = app.generation;
    e.instance = instance;
    e.hasPlane = false;
    operandHashCache_[app.id] = computeOperandHash(app);
}

data::Result<render::ReCsgObject> CsgObjectMapper::map(const scene::CsgObject& app,
                                                       const scene::TranslateContext& ctx) const {
    (void)ctx;
    if (!registry_) {
        return data::makeError<render::ReCsgObject>(data::ErrorDomain::Render, 4,
                                                    "CsgObjectMapper: null AssetRegistry");
    }
    if (!app.base.mesh) {
        return data::makeError<render::ReCsgObject>(data::ErrorDomain::Render, 1, "CsgObjectMapper: null base mesh asset reference");
    }
    // Base handle dedup by ContentHash (not pointer)
    auto hBase = registry_->registerAsset(*app.base.mesh);
    if (hBase.failed()) {
        return data::makeError<render::ReCsgObject>(hBase.error().code, hBase.error().message);
    }
    render::ReCsgObject out;
    out.baseHandle = *hBase;
    // Model is object.transform * base.operandTransform (per-operand base transform folded)
    out.model = app.transform * app.base.operandTransform;

    // World bounds derived from base mesh local bounds transformed by model — keep bounds and worldBounds in sync for task T6 ReCsgObject worldBounds naming while preserving ReMeshObject bounds parity (both alias same derived AABB so grep worldBounds and existing bounds call sites remain valid, see broker/csg_object_mapper.hpp ReCsgObject worldBounds field for the single derived allocation rationale).
    re::data::Aabb local = computeLocalAabb(*app.base.mesh);
    out.bounds = transformAabb(local, out.model);
    out.worldBounds = out.bounds;

    // Subtractors: per-operand operandTransform preserved, dedup by ContentHash
    out.subHandles.reserve(app.subtractors.size());
    out.subTransforms.reserve(app.subtractors.size());
    for (auto& op : app.subtractors) {
        if (!op.mesh) {
            return data::makeError<render::ReCsgObject>(data::ErrorDomain::Render, 1, "CsgObjectMapper: null subtractor mesh");
        }
        auto h = registry_->registerAsset(*op.mesh);
        if (h.failed()) {
            return data::makeError<render::ReCsgObject>(h.error().code, h.error().message);
        }
        out.subHandles.push_back(*h);
        out.subTransforms.push_back(op.operandTransform);
    }
    // Paints
    out.paintHandles.reserve(app.paints.size());
    out.paintTransforms.reserve(app.paints.size());
    out.paintBlends.reserve(app.paints.size());
    out.paintInteriorFlags.reserve(app.paints.size());
    for (auto& p : app.paints) {
        if (!p.oper.mesh) {
            return data::makeError<render::ReCsgObject>(data::ErrorDomain::Render, 1, "CsgObjectMapper: null paint mesh");
        }
        auto h = registry_->registerAsset(*p.oper.mesh);
        if (h.failed()) {
            return data::makeError<render::ReCsgObject>(h.error().code, h.error().message);
        }
        out.paintHandles.push_back(*h);
        out.paintTransforms.push_back(p.oper.operandTransform);
        out.paintBlends.push_back(p.blend);
        out.paintInteriorFlags.push_back(p.paintInterior);
    }
    return data::makeValue<render::ReCsgObject>(std::move(out));
}

} // namespace re::broker
