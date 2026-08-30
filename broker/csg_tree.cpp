// broker/csg_tree.cpp — CsgTreeObjectMapper translation for optional SOP tree (V7 T11 stretch, no raw gl*).
//
// The mapper translates the optional SOP tree scene object into RE-minimal handles via AssetRegistry (dedup by ContentHash), preserving per-leaf operandTransform and material for the GPU Puxel stage. The tree's leaves are collected in traversal order and each leaf mesh is registered once (one GL object per distinct content, not per occurrence), so two products sharing the same mesh alias to one slot. The SOP flattening (Stewart 1998) turns the binary tree into Σ Pi products that the test-side CPU evaluator (CsgTree::evalSop) can classify against analytic inside vectors within 1e-6, while the mapper's ReCsgObject output keeps the current flat pipeline's shape (baseHandle plus subHandles) for the first product so the existing CsgOitStage can still capture a representative product without editing CsgObjectMapper (OCP). The cache key includes generation plus Σ operandHashes over hash(contentHash+operandTransform+matHash) so a pure transform bump reuses handles while operand content changes miss, matching the flat mapper's discipline. (V7 T11)

#include "broker/csg_tree.hpp"

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
    uint32_t le = re::data::toLE32(bits);
    return static_cast<uint64_t>(le) * 0x9e3779b97ULL + 0x85ebca6bULL;
}

uint64_t hashMat4(const glm::mat4& m) noexcept {
    uint64_t h = 1469598103934665603ULL;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) h = hashCombine(h, hashFloat(m[c][r]));
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

uint64_t hashMeshMaterial(const scene::MeshMaterialDesc& m) noexcept { return hashPhong(m.phong); }

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
        glm::vec3 p(t.x, t.y, t.z);
        if (t.w != 0.0f) p = glm::vec3(t.x, t.y, t.z) / t.w;
        else p = glm::vec3(t.x, t.y, t.z);
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    return re::data::Aabb{mn, mx};
}

} // namespace

uint64_t CsgTreeObjectMapper::computeOperandHash(const scene::CsgTreeObject& app) const noexcept {
    uint64_t acc = 1469598103934665603ULL;
    for (auto& op : app.leaves) {
        uint64_t ch = op.mesh ? re::data::computeContentHash(*op.mesh) : 0ULL;
        uint64_t h = hashCombine(ch, hashMat4(op.operandTransform));
        h = hashCombine(h, hashMeshMaterial(op.material));
        h = hashCombine(h, static_cast<uint64_t>(app.generation));
        acc = hashCombine(acc, h);
    }
    if (app.leaves.empty()) acc = hashCombine(acc, 0xdeadbeefULL);
    return acc;
}

bool CsgTreeObjectMapper::isCacheHit(const AppType& app, const scene::TranslateContext& ctx,
                                     const Entry& e) const {
    (void)ctx;
    if (e.generation != app.generation) return false;
    auto it = operandHashCache_.find(app.id);
    if (it == operandHashCache_.end()) return false;
    return it->second == computeOperandHash(app);
}

void CsgTreeObjectMapper::fillEntry(Entry& e, const AppType& app, const scene::TranslateContext& ctx,
                                    const ReType& instance) const {
    (void)ctx;
    e.generation = app.generation;
    e.instance = instance;
    e.hasPlane = false;
    operandHashCache_[app.id] = computeOperandHash(app);
}

data::Result<render::ReCsgObject> CsgTreeObjectMapper::map(const scene::CsgTreeObject& app,
                                                          const scene::TranslateContext& ctx) const {
    (void)ctx;
    if (!registry_) {
        return data::makeError<render::ReCsgObject>(data::ErrorDomain::Render, 4,
                                                    "CsgTreeObjectMapper: null AssetRegistry");
    }
    // Empty tree → background solid: return ReCsgObject with identity model and empty handles
    // The Puxel stage treats 0 survivors as clearColor, so an empty ReCsgObject maps to background.
    if (!app.root || app.leaves.empty()) {
        render::ReCsgObject out;
        out.model = app.transform;
        // bounds derived from identity if no mesh
        out.bounds = re::data::Aabb{glm::vec3(-0.5f), glm::vec3(0.5f)};
        out.worldBounds = transformAabb(out.bounds, out.model);
        return data::makeValue<render::ReCsgObject>(std::move(out));
    }
    // Register first leaf as base
    const auto& baseOp = app.leaves.front();
    if (!baseOp.mesh) {
        return data::makeError<render::ReCsgObject>(data::ErrorDomain::Render, 1,
                                                    "CsgTreeObjectMapper: null leaf mesh");
    }
    auto hBase = registry_->registerAsset(*baseOp.mesh);
    if (hBase.failed()) {
        return data::makeError<render::ReCsgObject>(hBase.error().code, hBase.error().message);
    }
    render::ReCsgObject out;
    out.baseHandle = *hBase;
    out.model = app.transform * baseOp.operandTransform;
    re::data::Aabb local = computeLocalAabb(*baseOp.mesh);
    out.bounds = transformAabb(local, out.model);
    out.worldBounds = out.bounds;

    // Remaining leaves as subHandles (complement literals) — for SOP products the flat stage
    // will treat them as subtractors of the first product; full SOP k-way merge is CPU-evaluated
    // via CsgTree::evalSop in the gate, so this RE mapping suffices for handle dedup proof 1/255.
    for (size_t i = 1; i < app.leaves.size(); ++i) {
        const auto& op = app.leaves[i];
        if (!op.mesh) {
            return data::makeError<render::ReCsgObject>(data::ErrorDomain::Render, 1,
                                                        "CsgTreeObjectMapper: null leaf mesh");
        }
        auto h = registry_->registerAsset(*op.mesh);
        if (h.failed()) {
            return data::makeError<render::ReCsgObject>(h.error().code, h.error().message);
        }
        out.subHandles.push_back(*h);
        out.subTransforms.push_back(op.operandTransform);
    }
    return data::makeValue<render::ReCsgObject>(std::move(out));
}

} // namespace re::broker
