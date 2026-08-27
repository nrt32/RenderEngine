// render/asset_registry.cpp — the unified typed multi-kind GPU asset store:
// one GL object per distinct asset CONTENT, globally across every renderer
// that resolves through it. Mesh geometry, volume textures, image textures,
// and canonical material values each live in their own generational,
// ref-counted slot table keyed by content hash (never by CPU pointer), with
// typed stale-handle errors on every lookup path.

#include "render/asset_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "data/content_hash.hpp"

namespace re::render {

namespace {

// The typed-error codes shared by every handle-validation path (SPEC §5):
// 1 out-of-range index, 2 stale/dangling handle (generation or content-hash
// mismatch), 3 freed slot; code 4 additionally marks a null CPU asset input.
constexpr int kIndexOutOfRangeCode = 1;
constexpr int kGenerationMismatchCode = 2;
constexpr int kFreedSlotCode = 3;
constexpr int kNullAssetCode = 4;

// FNV-1a content hashes of the CPU asset bytes are NOT defined here anymore:
// the single GL-free definition lives in data/content_hash.hpp
// (data::computeContentHash overloads), shared verbatim with the app-side
// scene::AssetId identity so both layers agree on one asset's hash. Only the
// material kind's hash stays local — it is defined on the RE-side
// PhongMaterial VALUE and deliberately has no scene/ counterpart.

// FNV-1a hash of a PhongMaterial's VALUE — every shading-relevant field in a
// fixed order (baseColor xyzw, specular rgb, shininess, ambient, diffuse, as
// raw float32 bit patterns). This is the material kind's content identity:
// two distinct PhongMaterial allocations with identical field values produce
// identical hashes, while any differing value (including the alpha that
// drives isTransparent, FR-render.3) produces a different hash. There is no
// scene/ counterpart by design: identity is defined on the RE-side material
// VALUE — exactly what crosses into the render layer (RE-minimal, §12.4).
uint64_t materialContentHash(const PhongMaterial& m) noexcept {
    uint64_t h = 1469598103934665603ULL;
    auto feedF32 = [&](float v) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
        for (std::size_t i = 0; i < sizeof(float); ++i) {
            h ^= static_cast<uint64_t>(b[i]);
            h *= 1099511628211ULL;
        }
    };
    const glm::vec4 base = m.baseColor();
    feedF32(base.x);
    feedF32(base.y);
    feedF32(base.z);
    feedF32(base.w);
    feedF32(m.specular.r);
    feedF32(m.specular.g);
    feedF32(m.specular.b);
    feedF32(m.shininess);
    feedF32(m.ambient);
    feedF32(m.diffuse);
    return h;
}

// ---------------------------------------------------------------------------
// Kind-specific uploads (moved here from the former per-renderer caches so the
// store is the single place that turns CPU asset bytes into GPU objects).
// ---------------------------------------------------------------------------

/// Upload `dataset`'s float32 voxel grid into a fresh GL_R32F 3D texture
/// (GL_LINEAR trilinear filtering, GL_CLAMP_TO_EDGE — core::Texture3D's
/// upload-time defaults), so the ray-cast shader's `(idx+0.5)/dim` texcoord
/// reproduces the CPU trilinear sample.
data::Result<std::unique_ptr<core::Texture3D>> createVolumeTexture(
    const data::VolumeDataset& dataset) {
    auto texture = core::Texture3D::create();
    if (texture.failed()) {
        return data::makeError<std::unique_ptr<core::Texture3D>>(
            texture.error().code, texture.error().message);
    }
    auto owned = std::make_unique<core::Texture3D>(std::move(*texture));
    owned->bind(0u);
    owned->upload(dataset.sizeX(), dataset.sizeY(), dataset.sizeZ(),
                  dataset.voxels().data());
    owned->unbind(0u);
    return data::makeValue<std::unique_ptr<core::Texture3D>>(std::move(owned));
}

/// Byte-per-pixel sizes of the image channel counts the image kind accepts
/// (4 = RGBA, 3 = RGB, 1 = grayscale). Any other channel count is rejected
/// with a typed error at upload time.
bool supportedImageChannels(std::int32_t channels) noexcept {
    return channels == 1 || channels == 3 || channels == 4;
}

/// Convert an image's pixels to RGBA8 bytes for GL upload: 4-channel images
/// pass through, 3-channel images get alpha 255, 1-channel (grayscale) images
/// replicate the value to RGB with alpha 255. The result is laid out like
/// core::Texture2D expects (row 0 = bottom). Moved verbatim from the former
/// PlaneRenderer::imageToRgba8 — the row flip is part of the GPU upload
/// contract, not app-side geometry parsing.
std::vector<std::uint8_t> imageToRgba8(const data::Image& image) {
    const std::int32_t width = image.width();
    const std::int32_t height = image.height();
    const std::int32_t channels = image.channels();
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
        0u);

    // The source image uses a top-left origin (data::Image, stb convention),
    // while core::Texture2D expects row 0 = the BOTTOM scanline (GL
    // convention). Flipping rows here makes the image's top row the quad's
    // top (the v direction of the quad increases upward), so the image's
    // top-left pixel lands at the quad's top-left corner when viewed from the
    // normal's side.
    for (std::int32_t y = 0; y < height; ++y) {
        const std::int32_t glRow = height - 1 - y;
        for (std::int32_t x = 0; x < width; ++x) {
            const std::size_t src = static_cast<std::size_t>(y * width + x) *
                                    static_cast<std::size_t>(channels);
            const std::size_t dst =
                static_cast<std::size_t>(glRow * width + x) * 4u;
            if (channels == 4) {
                rgba[dst + 0u] = image.pixels()[src + 0u];
                rgba[dst + 1u] = image.pixels()[src + 1u];
                rgba[dst + 2u] = image.pixels()[src + 2u];
                rgba[dst + 3u] = image.pixels()[src + 3u];
            } else if (channels == 3) {
                rgba[dst + 0u] = image.pixels()[src + 0u];
                rgba[dst + 1u] = image.pixels()[src + 1u];
                rgba[dst + 2u] = image.pixels()[src + 2u];
                rgba[dst + 3u] = 255u;
            } else { // 1 channel (grayscale)
                const std::uint8_t v = image.pixels()[src];
                rgba[dst + 0u] = v;
                rgba[dst + 1u] = v;
                rgba[dst + 2u] = v;
                rgba[dst + 3u] = 255u;
            }
        }
    }
    return rgba;
}

/// Convert `image` to RGBA8 and upload it into a fresh 2D texture
/// (GL_LINEAR / CLAMP_TO_EDGE per core::Texture2D defaults, so a quad mapped
/// 1:1 onto the viewport reproduces the source texels exactly).
data::Result<std::unique_ptr<core::Texture2D>> createImageTexture(
    const data::Image& image) {
    if (!supportedImageChannels(image.channels())) {
        return data::makeError<std::unique_ptr<core::Texture2D>>(
            1, "AssetRegistry(image): unsupported image channel count (" +
                   std::to_string(image.channels()) +
                   "); supported: 1 (gray), 3 (RGB), 4 (RGBA)");
    }
    auto texture = core::Texture2D::create();
    if (texture.failed()) {
        return data::makeError<std::unique_ptr<core::Texture2D>>(
            texture.error().code, texture.error().message);
    }
    auto owned = std::make_unique<core::Texture2D>(std::move(*texture));
    const std::vector<std::uint8_t> rgba =
        imageToRgba8(image); // already flipped to GL bottom-up rows
    owned->bind(0u);
    owned->upload(static_cast<std::uint32_t>(image.width()),
                  static_cast<std::uint32_t>(image.height()), rgba.data());
    owned->unbind(0u);
    return data::makeValue<std::unique_ptr<core::Texture2D>>(std::move(owned));
}

/// Clone `src`'s VALUE into a fresh canonical PhongMaterial (the material
/// kind's "upload"): the store-owned instance is byte-identical to the
/// registered value but heap-stable and owned by the slot, so resolvers share
/// one immutable canonical instead of each carrying its own copy.
data::Result<std::unique_ptr<IMaterial>> clonePhongMaterial(
    const PhongMaterial& src) {
    // PhongMaterial's base color is constructor-injected; every other field
    // is public and copied across. The copy is bit-exact (plain float
    // members), so resolveMaterial().baseColor() equals the registered
    // value's exactly.
    auto copy = std::make_unique<PhongMaterial>(src.baseColor());
    copy->ambient = src.ambient;
    copy->diffuse = src.diffuse;
    copy->specular = src.specular;
    copy->shininess = src.shininess;
    return data::makeValue<std::unique_ptr<IMaterial>>(
        std::unique_ptr<IMaterial>(std::move(copy)));
}

} // namespace

// ---------------------------------------------------------------------------
// Process-wide default instance (T14 renderer defaults).
// ---------------------------------------------------------------------------

namespace {
// Function-local static so first use creates the instance; resetShared()
// clears it while a GL context is current so its textures are deleted with
// valid GL state instead of during static destruction after context death.
std::shared_ptr<AssetRegistry>& sharedRegistryStorage() {
    static std::shared_ptr<AssetRegistry> instance =
        std::make_shared<AssetRegistry>();
    return instance;
}
} // namespace

std::shared_ptr<AssetRegistry> AssetRegistry::shared() {
    std::shared_ptr<AssetRegistry>& storage = sharedRegistryStorage();
    if (!storage) {
        storage = std::make_shared<AssetRegistry>(); // recreate after a reset
    }
    return storage;
}

void AssetRegistry::resetShared() {
    sharedRegistryStorage().reset();
}

// ---------------------------------------------------------------------------
// Mesh kind (V2 T3 / T7 API — now reference-counted like the other kinds).
// ---------------------------------------------------------------------------

data::Result<AssetHandle> AssetRegistry::registerAsset(const data::Mesh& mesh) {
    // Content-hash dedup (primary): identical bytes alias even for distinct
    // pointers. Pointer-identity byObject_ is retained as dual-key shim
    // (diagnostics only); hash is the binding key from SceneStore::AssetId.
    const uint64_t hash = data::computeContentHash(mesh);
    auto hashIt = byHash_.find(hash);
    if (hashIt != byHash_.end()) {
        const AssetHandle& existing = hashIt->second;
        if (existing.index < slots_.size()) {
            Slot& slot = slots_[existing.index];
            if (slot.generation == existing.generation && slot.geometry &&
                slot.contentHash == hash) {
                ++slot.refs; // one more owner of the same GPU object
                return data::makeValue<AssetHandle>(existing);
            }
        }
    }
    // Fallback pointer shim (dual-key diagnostic) — same pointer dedup only
    // while the live slot actually HOLDS this content (`slot.contentHash ==
    // hash`): if the caller mutated the mesh through its non-const reference
    // after registering it, the hash no longer matches the slot's bytes and a
    // NEW geometry must be uploaded instead of aliasing stale GPU data (the
    // T14 invalidation rule). Unreachable while slots outlive their map
    // entries, but kept so a stale byHash_ entry can never cause a second
    // upload of a still-live object.
    const auto existing = byObject_.find(&mesh);
    if (existing != byObject_.end()) {
        const AssetHandle& shimHandle = existing->second;
        if (shimHandle.index < slots_.size()) {
            Slot& slot = slots_[shimHandle.index];
            if (slot.geometry && slot.generation == shimHandle.generation &&
                slot.contentHash == hash) {
                ++slot.refs;
                return data::makeValue<AssetHandle>(shimHandle);
            }
        }
    }

    // Upload first so a failure never mutates the slot table.
    auto geometry = MeshGeometry::create(mesh);
    if (geometry.failed()) {
        return data::makeError<AssetHandle>(geometry.error().code,
                                            geometry.error().message);
    }

    constexpr uint32_t kFirstRef = 1u; // registration takes one reference
    std::size_t index = 0u;
    if (!freeIndices_.empty()) {
        // Reuse a freed slot: its generation was bumped at free time and is
        // bumped again here, so every handle to the previous occupant stays
        // stale.
        index = freeIndices_.back();
        freeIndices_.pop_back();
        Slot& slot = slots_[index];
        slot.geometry = std::make_unique<MeshGeometry>(std::move(*geometry));
        slot.cpuObject = &mesh; // diagnostic borrow (see Slot @note lifetime)
        slot.contentHash = hash;
        slot.refs = kFirstRef;
        ++slot.generation;
    } else {
        // Fresh slot: generation starts at 1 (0 is the never-allocated/null
        // handle marker).
        Slot slot;
        slot.geometry = std::make_unique<MeshGeometry>(std::move(*geometry));
        slot.cpuObject = &mesh; // diagnostic borrow (see Slot @note lifetime)
        slot.contentHash = hash;
        slot.refs = kFirstRef;
        ++slot.generation;
        index = slots_.size();
        slots_.push_back(std::move(slot));
    }

    ++liveCount_;
    const AssetHandle handle{static_cast<std::uint32_t>(index),
                             slots_[index].generation};
    byObject_.emplace(&mesh, handle);
    byHash_[hash] = handle;
    return data::makeValue<AssetHandle>(handle);
}

data::Result<MeshGeometry*> AssetRegistry::resolve(const AssetHandle& handle) {
    if (handle.index >= slots_.size()) {
        return data::makeError<MeshGeometry*>(
            kIndexOutOfRangeCode, "AssetRegistry: handle index " +
                                      std::to_string(handle.index) +
                                      " out of range (slot table size " +
                                      std::to_string(slots_.size()) + ")");
    }
    const Slot& slot = slots_[handle.index];
    if (slot.generation != handle.generation) {
        return data::makeError<MeshGeometry*>(
            kGenerationMismatchCode,
            "AssetRegistry: stale handle (generation " +
                std::to_string(handle.generation) + " != slot generation " +
                std::to_string(slot.generation) + ")");
    }
    if (!slot.geometry) {
        return data::makeError<MeshGeometry*>(
            kFreedSlotCode, "AssetRegistry: handle references a freed slot");
    }
    return data::makeValue<MeshGeometry*>(slot.geometry.get());
}

data::Result<void> AssetRegistry::unregister(const AssetHandle& handle) {
    if (handle.index >= slots_.size()) {
        return data::makeError<void>(
            kIndexOutOfRangeCode, "AssetRegistry: handle index out of range");
    }
    Slot& slot = slots_[handle.index];
    if (slot.generation != handle.generation) {
        return data::makeError<void>(
            kGenerationMismatchCode,
            "AssetRegistry: stale handle (generation mismatch)");
    }
    if (!slot.geometry) {
        return data::makeError<void>(kFreedSlotCode,
                                     "AssetRegistry: handle references a freed "
                                     "slot");
    }
    if (slot.refs > 0u) {
        --slot.refs;
    }
    // The diagnostic borrow may die independently of the slot's remaining
    // references, so the pointer-shim entry is dropped on EVERY release; the
    // content hash remains the binding dedup key.
    if (slot.cpuObject != nullptr) {
        byObject_.erase(slot.cpuObject);
        slot.cpuObject = nullptr;
    }
    if (slot.refs != 0u) {
        return data::Result<void>(data::value); // other owners keep it alive
    }
    byHash_.erase(slot.contentHash);
    slot.geometry.reset(); // destroys the GPU object (last reference)
    slot.contentHash = 0u;
    ++slot.generation; // every outstanding handle to this slot is now stale
    freeIndices_.push_back(handle.index);
    --liveCount_;
    return data::Result<void>(data::value);
}

// ---------------------------------------------------------------------------
// Volume kind: data::VolumeDataset → core::Texture3D (T14).
// ---------------------------------------------------------------------------

data::Result<VolumeTextureHandle> AssetRegistry::registerVolume(
    const std::shared_ptr<const data::VolumeDataset>& dataset) {
    if (!dataset) {
        return data::makeError<VolumeTextureHandle>(
            kNullAssetCode, "AssetRegistry(volume): null dataset shared_ptr");
    }
    const uint64_t hash = data::computeContentHash(*dataset);
    auto loc = volumes_.acquire(dataset, hash,
                                /*claimRefs=*/1u, &createVolumeTexture);
    if (loc.failed()) {
        return data::makeError<VolumeTextureHandle>(loc.error().code,
                                                    loc.error().message);
    }
    return data::makeValue<VolumeTextureHandle>(
        VolumeTextureHandle{loc->index, loc->generation, hash});
}

data::Result<core::Texture3D*> AssetRegistry::resolveVolume(
    const VolumeTextureHandle& handle) const {
    return volumes_.resolve(handle.index, handle.generation, handle.contentHash);
}

data::Result<void> AssetRegistry::unregisterVolume(
    const VolumeTextureHandle& handle) {
    return volumes_.release(handle.index, handle.generation,
                            handle.contentHash);
}

data::Result<std::uint32_t> AssetRegistry::volumeRefs(
    const VolumeTextureHandle& handle) const {
    return volumes_.refsAt(handle.index, handle.generation,
                           handle.contentHash);
}

// ---------------------------------------------------------------------------
// Image kind: data::Image → core::Texture2D (T14).
// ---------------------------------------------------------------------------

data::Result<ImageTextureHandle> AssetRegistry::registerImage(
    const std::shared_ptr<const data::Image>& image) {
    if (!image) {
        return data::makeError<ImageTextureHandle>(
            kNullAssetCode, "AssetRegistry(image): null image shared_ptr");
    }
    const uint64_t hash = data::computeContentHash(*image);
    auto loc =
        images_.acquire(image, hash, /*claimRefs=*/1u, &createImageTexture);
    if (loc.failed()) {
        return data::makeError<ImageTextureHandle>(loc.error().code,
                                                   loc.error().message);
    }
    return data::makeValue<ImageTextureHandle>(
        ImageTextureHandle{loc->index, loc->generation, hash});
}

data::Result<core::Texture2D*> AssetRegistry::resolveImage(
    const ImageTextureHandle& handle) const {
    return images_.resolve(handle.index, handle.generation, handle.contentHash);
}

data::Result<void> AssetRegistry::unregisterImage(
    const ImageTextureHandle& handle) {
    return images_.release(handle.index, handle.generation, handle.contentHash);
}

data::Result<std::uint32_t> AssetRegistry::imageRefs(
    const ImageTextureHandle& handle) const {
    return images_.refsAt(handle.index, handle.generation, handle.contentHash);
}

// ---------------------------------------------------------------------------
// Material kind: PhongMaterial value → canonical IMaterial (T14).
// ---------------------------------------------------------------------------

data::Result<MaterialHandle> AssetRegistry::registerMaterial(
    const std::shared_ptr<const PhongMaterial>& material) {
    if (!material) {
        return data::makeError<MaterialHandle>(
            kNullAssetCode, "AssetRegistry(material): null material shared_ptr");
    }
    const uint64_t hash = materialContentHash(*material);
    auto loc = materials_.acquire(material, hash,
                                  /*claimRefs=*/1u, &clonePhongMaterial);
    if (loc.failed()) {
        return data::makeError<MaterialHandle>(loc.error().code,
                                               loc.error().message);
    }
    return data::makeValue<MaterialHandle>(
        MaterialHandle{loc->index, loc->generation, hash});
}

data::Result<IMaterial*> AssetRegistry::resolveMaterial(
    const MaterialHandle& handle) const {
    return materials_.resolve(handle.index, handle.generation,
                              handle.contentHash);
}

data::Result<void> AssetRegistry::unregisterMaterial(
    const MaterialHandle& handle) {
    return materials_.release(handle.index, handle.generation,
                              handle.contentHash);
}

data::Result<std::uint32_t> AssetRegistry::materialRefs(
    const MaterialHandle& handle) const {
    return materials_.refsAt(handle.index, handle.generation,
                             handle.contentHash);
}

// ---------------------------------------------------------------------------
// Shared mesh-geometry resolution (mesh-family renderers' former geometryFor).
// ---------------------------------------------------------------------------

data::Result<MeshGeometry*> resolveMeshGeometry(
    const std::shared_ptr<AssetRegistry>& registry, const AssetHandle& handle,
    std::string_view rendererName) {
    if (!registry) {
        // Typed error (code 4), never a null dereference: a renderer built
        // with a null registry (possible only by explicit request — member
        // init order can never produce one, T13) fails loudly per draw.
        return data::makeError<MeshGeometry*>(
            4, std::string(rendererName) + ": no asset registry injected");
    }
    return registry->resolve(handle);
}

} // namespace re::render
