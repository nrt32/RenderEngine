// broker/camera_mapper.cpp — CameraMapper implementation: map the app-side
// scene::Camera onto render::Camera (view/proj/eye). The mapper also VALIDATES
// the 2D/3D pairing: when the context carries a slice plane the projection
// must be orthographic; with no plane it must be perspective — a mismatch is
// a typed error, not silently wrong geometry. mapCached memoizes per camera
// identity: the entry key is a CompositeKey over the owning view's id, both
// per-field generations and an FNV-1a fingerprint of the camera's stable
// parameter bytes, so a hit implies byte-identical input and independent
// cameras never evict or serve each other (the old single-slot cache keyed
// only on generations collided across views at generation zero).

#include "broker/camera_mapper.hpp"

#include <cstdint>

#include "data/result.hpp"

namespace re::broker {
namespace {

/// The shared 2D/3D pairing validation (plane present -> ortho). Returns the
/// typed error for a mismatch, or success.
data::Result<void> validateProjectionPairing(const scene::Camera& app,
                                             const scene::TranslateContext& ctx) {
    const bool hasPlane = ctx.hasPlane();
    if (hasPlane && app.isPerspective()) {
        return data::makeError<void>(
            4, "2D view with plane requires orthographic camera (plane "
               "present -> ortho)");
    }
    if (!hasPlane && app.isOrthographic()) {
        return data::makeError<void>(
            4, "3D view without plane requires perspective camera (no plane "
               "-> perspective)");
    }
    return data::Result<void>(data::value);
}

// Bit-cast a float to its IEEE-754 bit pattern as one hashable word. The bit
// image is injective, so distinct parameter bytes stay distinct fingerprint
// bytes (-0.0f and 0.0f hash differently; both are valid stable states).
uint64_t hashWord(float f) noexcept {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(f), "float must be 32-bit");
    __builtin_memcpy(&bits, &f, sizeof(bits));
    return bits;
}

} // namespace

scene::CompositeKey CameraMapper::makeKey(const scene::Camera& app,
                                          const scene::TranslateContext& ctx) noexcept {
    // Stable fingerprint words: every input that can change the translated
    // matrices — eye/center/up, projection mode, BOTH per-field generations.
    // Hashed once through CompositeKey::hashStableBytes (FNV-1a over the
    // canonical byte sequence): the project's one content-hash definition,
    // and no struct memcpy so padding bytes never enter the fingerprint.
    const uint64_t words[] = {
        hashWord(app.eye().x),    hashWord(app.eye().y),
        hashWord(app.eye().z),    hashWord(app.center().x),
        hashWord(app.center().y), hashWord(app.center().z),
        hashWord(app.up().x),     hashWord(app.up().y),
        hashWord(app.up().z),     static_cast<uint64_t>(app.projectionType()),
        app.viewGen(),            app.projGen(),
    };
    scene::CompositeKey key;
    key.version = 1;  // cache-entry schema (see header comment)
    key.layoutId = 0; // cameras are view-scoped, not layout-scoped
    key.id = ctx.view.viewId;
    key.gen = app.generation(); // max(viewGen, projGen)
    key.hash = scene::CompositeKey::hashStableBytes(words, sizeof(words));
    key.typeHash = 0; // single typed use site; type identity is the mapper's
    return key;
}

data::Result<render::Camera> CameraMapper::map(
    const scene::Camera& app, const scene::TranslateContext& ctx) const {
    auto valid = validateProjectionPairing(app, ctx);
    if (valid.failed()) {
        return data::makeError<render::Camera>(valid.error().code,
                                               valid.error().message);
    }
    render::Camera out;
    out.view = app.viewMatrix();
    out.proj = app.projMatrix();
    out.position = app.eye();
    return data::makeValue<render::Camera>(out);
}

data::Result<render::Camera> CameraMapper::mapCached(
    const scene::Camera& app, const scene::TranslateContext& ctx) {
    // Validation first — a cache hit must never bypass the 2D/3D pairing
    // check (T4 gate). Validation reads only ctx/plane flags and projection
    // mode, so it stays correct independently of the memo below.
    auto valid = validateProjectionPairing(app, ctx);
    if (valid.failed()) {
        return data::makeError<render::Camera>(valid.error().code,
                                               valid.error().message);
    }
    const scene::CompositeKey key = makeKey(app, ctx);
    auto it = cache_.find(key.id);
    if (it != cache_.end() && it->second.key == key) {
        ++hits_; // test evidence: same identity served without re-translation
        return data::makeValue<render::Camera>(it->second.value);
    }
    render::Camera out;
    out.view = app.viewMatrix();
    out.proj = app.projMatrix();
    out.position = app.eye();
    cache_[key.id] = Memo{key, out}; // replace THIS view's slot only
    ++misses_;
    return data::makeValue<render::Camera>(out);
}

void CameraMapper::invalidate(uint64_t id) {
    // Id-keyed eviction: drop exactly this view's slot; every other camera's
    // memo survives (the cross-camera thrash the former single-slot cache
    // could not avoid).
    cache_.erase(id);
}

} // namespace re::broker
