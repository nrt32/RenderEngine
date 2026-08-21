// tests/t4_io_data_test.cpp — T4 gate tests (io/ loaders + data/ containers).
//
// Asserts the io/ + data/ mesh & image requirements from TASKS.md T4:
//   (1) bunny.obj loads with its known hand-counted vertex/index counts and
//       AABB (FR-io.1);
//   (2) a golden fixture mesh has exact expected bounds (FR-data.2);
//   (3) the face normal of a known triangle equals the closed-form
//       cross-product value (FR-data.1);
//   (4) the image loader returns known dimensions + corner/center pixel
//       values (FR-io.3);
//   (5) malformed input returns a typed error and leaves no partial state
//       (FR-io.4).
//
// All acceptance constants are hand-counted from the committed golden files
// and recorded in data/README.md and docs/io-data.md:
//   - bunny.obj: 35,947 vertices, 69,451 faces (208,353 indices), AABB
//     min (-0.094690, 0.032987, -0.061874), max (0.061009, 0.187321, 0.058800);
//   - golden_box.obj: 8 vertices, 12 faces, AABB min (0,0,0), max (1,1,1),
//     outward unit face normals (right-hand rule);
//   - golden_image.png: 8x8 RGB, pixel(x,y) = (32*x, 32*y, 128).

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <string>

#include "data/image.hpp"
#include "data/mesh.hpp"
#include "io/image/image_loader.hpp"
#include "io/mesh/obj_mesh_loader.hpp"

namespace re::tests {
namespace {

// Repo-root-relative path resolution (tests run from the build dir).
std::string assetPath(const std::string& rel) {
    return std::string(TEST_SOURCE_DIR) + "/" + rel;
}

// Write `contents` to a uniquely-named scratch file under the system temp
// dir and return its path (used for malformed-input fixtures, FR-io.4).
std::filesystem::path writeTempFile(const std::string& contents) {
    auto path = std::filesystem::temp_directory_path() /
                ("re_t4_fixture_" + std::to_string(::getpid()) + ".obj");
    std::ofstream out(path);
    out << contents;
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) FR-io.1 — bunny.obj loads with hand-counted counts and AABB.
// ---------------------------------------------------------------------------
TEST(T4IoData, BunnyObjLoadsWithKnownCountsAndBounds) {
    auto result = io::loadObjMesh(assetPath("data/meshes/bunny.obj"));
    ASSERT_TRUE(result.ok()) << result.error().message;

    const data::Mesh& mesh = *result;
    // Hand-counted from the committed file (data/README.md, docs/io-data.md).
    EXPECT_EQ(mesh.vertexCount(), 35947u);
    EXPECT_EQ(mesh.triangleCount(), 69451u);
    EXPECT_EQ(mesh.indices().size(), 208353u); // 3 * 69,451

    // Analytic AABB of the committed file (float32, hand-computed):
    // min (-0.094690, 0.032987, -0.061874), max (0.061009, 0.187321, 0.058800).
    const data::Aabb& b = mesh.bounds();
    EXPECT_FLOAT_EQ(b.min.x, -0.094690f);
    EXPECT_FLOAT_EQ(b.min.y, 0.032987f);
    EXPECT_FLOAT_EQ(b.min.z, -0.061874f);
    EXPECT_FLOAT_EQ(b.max.x, 0.061009f);
    EXPECT_FLOAT_EQ(b.max.y, 0.187321f);
    EXPECT_FLOAT_EQ(b.max.z, 0.058800f);
}

// ---------------------------------------------------------------------------
// (2) FR-data.2 — golden fixture mesh has exact expected bounds.
// ---------------------------------------------------------------------------
TEST(T4IoData, GoldenBoxHasExactBounds) {
    auto result = io::loadObjMesh(assetPath("data/fixtures/golden_box.obj"));
    ASSERT_TRUE(result.ok()) << result.error().message;

    const data::Mesh& mesh = *result;
    // Hand-counted: 8 vertices, 12 triangle faces, AABB [0,1]^3.
    EXPECT_EQ(mesh.vertexCount(), 8u);
    EXPECT_EQ(mesh.triangleCount(), 12u);

    const data::Aabb& b = mesh.bounds();
    EXPECT_FLOAT_EQ(b.min.x, 0.0f);
    EXPECT_FLOAT_EQ(b.min.y, 0.0f);
    EXPECT_FLOAT_EQ(b.min.z, 0.0f);
    EXPECT_FLOAT_EQ(b.max.x, 1.0f);
    EXPECT_FLOAT_EQ(b.max.y, 1.0f);
    EXPECT_FLOAT_EQ(b.max.z, 1.0f);
}

// ---------------------------------------------------------------------------
// (3) FR-data.1 — face normal equals the closed-form cross product.
// ---------------------------------------------------------------------------
TEST(T4IoData, FaceNormalEqualsClosedFormCrossProduct) {
    auto result = io::loadObjMesh(assetPath("data/fixtures/golden_box.obj"));
    ASSERT_TRUE(result.ok()) << result.error().message;

    const data::Mesh& mesh = *result;
    ASSERT_EQ(mesh.faceNormals().size(), 12u);

    // Golden-box vertex table (1-based, from the fixture header):
    //   1=(0,0,0) 2=(1,0,0) 3=(1,1,0) 4=(0,1,0)
    //   5=(0,0,1) 6=(1,0,1) 7=(1,1,1) 8=(0,1,1)
    // Closed form: n = normalize(cross(p1 - p0, p2 - p0)).
    // Face 0 = "f 1 4 3" -> (0,0,0),(0,1,0),(1,1,0) -> cross = (0,0,-1).
    // Face 2 = "f 5 6 7" -> (0,0,1),(1,0,1),(1,1,1) -> cross = (0,0,1).
    // Face 4 = "f 1 6 5" -> (0,0,0),(1,0,1),(0,0,1) -> cross = (0,-1,0).
    const glm::vec3 expected[] = {
        {0.0f, 0.0f, -1.0f}, // face 0: bottom (z=0), outward -Z
        {0.0f, 0.0f, 1.0f},  // face 2: top (z=1), outward +Z
        {0.0f, -1.0f, 0.0f}, // face 4: front (y=0), outward -Y
    };
    const std::size_t faces[] = {0, 2, 4};
    for (std::size_t i = 0; i < 3; ++i) {
        const glm::vec3& n = mesh.faceNormals()[faces[i]];
        EXPECT_FLOAT_EQ(n.x, expected[i].x) << "face " << faces[i];
        EXPECT_FLOAT_EQ(n.y, expected[i].y) << "face " << faces[i];
        EXPECT_FLOAT_EQ(n.z, expected[i].z) << "face " << faces[i];
    }

    // Invariant for all 12 faces: the cube's outward normals are unit length.
    for (std::size_t f = 0; f < 12; ++f) {
        const glm::vec3& n = mesh.faceNormals()[f];
        const float lengthSq = glm::dot(n, n);
        EXPECT_FLOAT_EQ(lengthSq, 1.0f) << "face " << f << " is not unit";
    }
}

// ---------------------------------------------------------------------------
// (4) FR-io.3 — image loader returns known dimensions + pixel values.
// ---------------------------------------------------------------------------
TEST(T4IoData, ImageLoaderKnownDimsAndPixelValues) {
    // Native channel count: the fixture is an 8x8 8-bit RGB PNG.
    auto result = io::loadImage(assetPath("data/fixtures/golden_image.png"));
    ASSERT_TRUE(result.ok()) << result.error().message;

    const data::Image& image = *result;
    EXPECT_EQ(image.width(), 8);
    EXPECT_EQ(image.height(), 8);
    EXPECT_EQ(image.channels(), 3);
    EXPECT_EQ(image.byteSize(), 8u * 8u * 3u);

    // Closed form (data/README.md): pixel(x,y) = (32*x, 32*y, 128). Assert
    // every one of the 64 pixels, plus the named corners/center explicitly.
    for (std::int32_t y = 0; y < image.height(); ++y) {
        for (std::int32_t x = 0; x < image.width(); ++x) {
            EXPECT_EQ(image.pixel(x, y, 0), static_cast<std::uint8_t>(32 * x))
                << "R at (" << x << "," << y << ")";
            EXPECT_EQ(image.pixel(x, y, 1), static_cast<std::uint8_t>(32 * y))
                << "G at (" << x << "," << y << ")";
            EXPECT_EQ(image.pixel(x, y, 2), 128)
                << "B at (" << x << "," << y << ")";
        }
    }
    // Named corners + center (README constants).
    EXPECT_EQ(image.pixel(0, 0, 0), 0);
    EXPECT_EQ(image.pixel(0, 0, 1), 0);
    EXPECT_EQ(image.pixel(0, 0, 2), 128);
    EXPECT_EQ(image.pixel(7, 0, 0), 224);
    EXPECT_EQ(image.pixel(7, 0, 1), 0);
    EXPECT_EQ(image.pixel(0, 7, 1), 224);
    EXPECT_EQ(image.pixel(7, 7, 0), 224);
    EXPECT_EQ(image.pixel(7, 7, 1), 224);
    EXPECT_EQ(image.pixel(7, 7, 2), 128);
    EXPECT_EQ(image.pixel(3, 3, 0), 96);
    EXPECT_EQ(image.pixel(3, 3, 1), 96);
    EXPECT_EQ(image.pixel(4, 4, 0), 128);
    EXPECT_EQ(image.pixel(4, 4, 1), 128);
    EXPECT_EQ(image.pixel(4, 4, 2), 128);
}

TEST(T4IoData, ImageLoaderRgbaConversion) {
    // Requesting 4 channels converts the RGB fixture to RGBA with alpha=255
    // (stb's documented conversion); the RGB channels keep their values.
    auto result = io::loadImage(assetPath("data/fixtures/golden_image.png"), 4);
    ASSERT_TRUE(result.ok()) << result.error().message;

    const data::Image& image = *result;
    EXPECT_EQ(image.width(), 8);
    EXPECT_EQ(image.height(), 8);
    EXPECT_EQ(image.channels(), 4);
    EXPECT_EQ(image.byteSize(), 8u * 8u * 4u);

    // Closed form with alpha: pixel(x,y) = (32*x, 32*y, 128, 255).
    for (std::int32_t y = 0; y < image.height(); ++y) {
        for (std::int32_t x = 0; x < image.width(); ++x) {
            EXPECT_EQ(image.pixel(x, y, 0), static_cast<std::uint8_t>(32 * x));
            EXPECT_EQ(image.pixel(x, y, 1), static_cast<std::uint8_t>(32 * y));
            EXPECT_EQ(image.pixel(x, y, 2), 128);
            EXPECT_EQ(image.pixel(x, y, 3), 255);
        }
    }
}

// ---------------------------------------------------------------------------
// (5) FR-io.4 — malformed input: typed error, no exception, no partial state.
// ---------------------------------------------------------------------------
TEST(T4IoData, MalformedMeshInputReturnsTypedError) {
    // (a) Nonexistent file -> FileOpen.
    {
        auto result =
            io::loadObjMesh(assetPath("data/meshes/does_not_exist.obj"));
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code,
                  static_cast<int>(io::MeshLoadError::FileOpen));
    }

    // (b) Face referencing an out-of-range vertex -> IndexRange.
    {
        const auto path = writeTempFile("v 0 0 0\nv 1 0 0\nf 1 2 3\n");
        auto result = io::loadObjMesh(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code,
                  static_cast<int>(io::MeshLoadError::IndexRange));
        std::filesystem::remove(path);
    }

    // (c) Malformed face index token -> FaceParse.
    {
        const auto path = writeTempFile("v 0 0 0\nv 1 0 0\nf 1 2 x\n");
        auto result = io::loadObjMesh(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code,
                  static_cast<int>(io::MeshLoadError::FaceParse));
        std::filesystem::remove(path);
    }

    // (d) Malformed vertex line -> VertexParse.
    {
        const auto path = writeTempFile("v abc 0 0\nf 1 1 1\n");
        auto result = io::loadObjMesh(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code,
                  static_cast<int>(io::MeshLoadError::VertexParse));
        std::filesystem::remove(path);
    }

    // (e) No faces -> NoFaces (a file with vertices only).
    {
        const auto path = writeTempFile("v 0 0 0\nv 1 0 0\n");
        auto result = io::loadObjMesh(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code,
                  static_cast<int>(io::MeshLoadError::NoFaces));
        std::filesystem::remove(path);
    }

    // FR-io.4 "no exception escape": every loader call above (including all
    // the successful loads in the other tests) must not throw. Re-run the
    // malformed cases through ASSERT_NO_THROW to make the guarantee explicit.
    const auto path = writeTempFile("v 0 0 0\nv 1 0 0\nf 1 2 3\n");
    ASSERT_NO_THROW(io::loadObjMesh(path.string()));
    ASSERT_NO_THROW(
        io::loadObjMesh(assetPath("data/meshes/does_not_exist.obj")));
    std::filesystem::remove(path);

    // "No partial state": the failed Result carries no Mesh at all — there is
    // nothing to dereference, and the loader builds the Mesh only after the
    // whole file validates (checked by construction in the cases above: each
    // failed call leaves `result.ok() == false`).
    auto result = io::loadObjMesh(assetPath("data/meshes/does_not_exist.obj"));
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code,
              static_cast<int>(io::MeshLoadError::FileOpen));
}

TEST(T4IoData, MalformedImageInputReturnsTypedError) {
    // (a) Nonexistent file -> FileOpen.
    {
        auto result =
            io::loadImage(assetPath("data/fixtures/does_not_exist.png"));
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code,
                  static_cast<int>(io::ImageLoadError::FileOpen));
    }

    // (b) A file whose bytes are not a decodable image -> Decode.
    {
        auto path = std::filesystem::temp_directory_path() /
                    ("re_t4_bad_" + std::to_string(::getpid()) + ".png");
        {
            std::ofstream out(path, std::ios::binary);
            out << "this is not a png image file, just garbage bytes\n";
        }
        auto result = io::loadImage(path.string());
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code,
                  static_cast<int>(io::ImageLoadError::Decode));
        std::filesystem::remove(path);
    }

    // (c) Invalid channel request -> InvalidChannels.
    {
        auto result =
            io::loadImage(assetPath("data/fixtures/golden_image.png"), 7);
        EXPECT_TRUE(result.failed());
        EXPECT_EQ(result.error().code,
                  static_cast<int>(io::ImageLoadError::InvalidChannels));
    }

    // FR-io.4 "no exception escape".
    ASSERT_NO_THROW(
        io::loadImage(assetPath("data/fixtures/does_not_exist.png")));
}

} // namespace re::tests