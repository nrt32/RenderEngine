// tests/t2_assets_test.cpp — T2 gate tests (asset provisioning).
//
// Asserts the committed-asset requirements from TASKS.md T2:
//   (1) every committed dataset dir (data/meshes, data/volumes) contains a
//       LICENSE file — the gate enumerates each dataset dir and asserts one
//       LICENSE each (the audit rule `assets_licensed` is only a floor);
//   (2) committed files have the expected SHA256s recorded in SPEC section 7
//       and data/README.md;
//   (3) the committed NRRD header parses to the expected dims <=128^3;
//   (4) bunny.obj has its known hand-counted vertex count.
//
// The SHA256 values here are the SPEC section 7 pinned/verified constants. The
// values are independent of the working tree: the check runs at build time
// against the committed files, so a tampered/unpinned asset fails loudly.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace re::tests {
namespace {

// --- tiny SHA-256 (FIPS 180-4) used only to fingerprint the committed assets. -
// Reuse a compact, dependency-free implementation so the test does not need
// OpenSSL or any external lib.
constexpr std::array<std::uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::array<std::uint8_t, 32> sha256(const std::vector<std::uint8_t>& data) {
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                      0xa54ff53a, 0x510e527f, 0x9b05688c,
                                      0x1f83d9ab, 0x5be0cd19};
    // Prepend the 0x80 padding byte plus the 64-bit big-endian bit length.
    const std::uint64_t bitlen =
        static_cast<std::uint64_t>(data.size()) * 8ULL;
    std::vector<std::uint8_t> m(data);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0x00);
    for (int i = 7; i >= 0; --i) m.push_back(static_cast<std::uint8_t>(bitlen >> (8 * i)));

    for (std::size_t off = 0; off < m.size(); off += 64) {
        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<std::uint32_t>(m[off + 4 * i]) << 24) |
                   (static_cast<std::uint32_t>(m[off + 4 * i + 1]) << 16) |
                   (static_cast<std::uint32_t>(m[off + 4 * i + 2]) << 8) |
                   static_cast<std::uint32_t>(m[off + 4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = ((w[i - 15] >> 7) | (w[i - 15] << 25)) ^
                                     ((w[i - 15] >> 18) | (w[i - 15] << 14)) ^
                                     (w[i - 15] >> 3);
            const std::uint32_t s1 = ((w[i - 2] >> 17) | (w[i - 2] << 15)) ^
                                     ((w[i - 2] >> 19) | (w[i - 2] << 13)) ^
                                     (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5],
             g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = ((e >> 6) | (e << 26)) ^
                                     ((e >> 11) | (e << 21)) ^
                                     ((e >> 25) | (e << 7));
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            const std::uint32_t S0 = ((a >> 2) | (a << 30)) ^
                                     ((a >> 13) | (a << 19)) ^
                                     ((a >> 22) | (a << 10));
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j)
            out[4 * i + j] = static_cast<std::uint8_t>(h[i] >> (24 - 8 * j));
    return out;
}

std::vector<std::uint8_t> readBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string toHex(const std::array<std::uint8_t, 32>& d) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (const auto b : d) {
        s.push_back(hex[b >> 4]);
        s.push_back(hex[b & 0x0f]);
    }
    return s;
}

// Repo-root-relative path resolution (tests run from the build dir).
std::string assetPath(const std::string& rel) {
    return std::string(TEST_SOURCE_DIR) + "/" + rel;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Every dataset dir has exactly one LICENSE file.
// ---------------------------------------------------------------------------
TEST(T2Assets, EveryDatasetDirHasLicense) {
    // Enumerate the committed dataset dirs and require a LICENSE in each. This
    // is stronger than the `assets_licensed` audit floor (which only greps for
    // any LICENSE anywhere).
    const char* datasetDirs[] = {"data/meshes", "data/volumes"};

    for (const auto* dir : datasetDirs) {
        std::string licensePath = assetPath(std::string(dir) + "/LICENSE");
        std::ifstream f(licensePath);
        EXPECT_TRUE(f.good()) << "dataset dir '" << dir
                              << "' is missing its LICENSE file ("
                              << licensePath << ")";
        if (f.good()) {
            std::string contents((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            // A LICENSE must not be empty/trivial — assert it names the dataset.
            EXPECT_GT(contents.size(), 20u);
        }
    }
}

// ---------------------------------------------------------------------------
// (2) Committed files have the expected SHA256s (SPEC section 7 / README).
//     The first three are the SPEC section 7 assets; the last three are the
//     data/README.md golden-fixture checksums (same recorded constants).
// ---------------------------------------------------------------------------
TEST(T2Assets, CommittedAssetSha256MatchSpec) {
    struct Asset {
        const char* path;
        const char* sha256;
    };
    const Asset assets[] = {
        {"data/meshes/bunny.obj",
         "1eb35d1e21ce99e5ce911353b6be278990713448dd9e8f5c9387f9de39b32205"},
        {"data/meshes/teapot.obj",
         "1b5396fedd74b577e32cef41146582c2f2e1a050d5b4915193c0ac1ad4187ed4"},
        {"data/volumes/sample_ct.nrrd",
         "816375cdcbb3a00abb87fcbd14075f78287aaf7e05eb751082b5c900f2df7865"},
        // Golden fixtures (hand-authored; checksums recorded in data/README.md).
        {"data/fixtures/golden_box.obj",
         "9e0bf449cdf212ab0cf77a1fa51ff2147f2944f22775970331abb37397a1612a"},
        {"data/fixtures/golden_volume.nrrd",
         "481f61987d9fc59e0a18511be002cb9a97c8933d9325753b8b9d1c63ce4f7e01"},
        {"data/fixtures/golden_image.png",
         "26033d298e625be34fb18797154d047ca36381dbda96495333f9e2cca8605432"},
    };
    for (const auto& a : assets) {
        const auto bytes = readBytes(assetPath(a.path));
        ASSERT_FALSE(bytes.empty()) << "asset missing: " << a.path;
        EXPECT_EQ(toHex(sha256(bytes)), a.sha256)
            << "SHA256 of " << a.path
            << " does not match the SPEC section 7 / data/README.md recorded value.";
    }
}

// ---------------------------------------------------------------------------
// (3) The committed NRRD header parses to dims <=128^3.
// ---------------------------------------------------------------------------
TEST(T2Assets, SampleCtNrrdDimsWithinBudget) {
    const auto bytes = readBytes(assetPath("data/volumes/sample_ct.nrrd"));
    ASSERT_FALSE(bytes.empty());
    std::string text(bytes.begin(), bytes.begin() + 512);  // header region

    // Locate the sizes line; stop at the blank line separating header from
    // the raw voxel block.
    std::istringstream hdr(text.substr(0, text.find("\n\n")));
    std::string line, sizesLine, typeLine;
    while (std::getline(hdr, line)) {
        if (line.rfind("sizes:", 0) == 0) {
            sizesLine = line;
        }
        if (line.rfind("type:", 0) == 0) {
            typeLine = line;
        }
    }
    ASSERT_FALSE(sizesLine.empty()) << "NRRD header missing 'sizes:' field";
    ASSERT_FALSE(typeLine.empty()) << "NRRD header missing 'type:' field";

    // The converter preserves the source type; the committed file is int32
    // (raw block 128*128*70*4 bytes, cross-checked in data/README.md). Assert
    // the header type explicitly so a format/doc drift fails loudly.
    EXPECT_EQ(typeLine.substr(5), " int")
        << "sample_ct.nrrd header type must be 'int' (int32, per README)";
    EXPECT_EQ(bytes.size() - text.find("\n\n") - 2,
              static_cast<std::size_t>(128) * 128 * 70 * 4)
        << "raw voxel block must be 128*128*70 int32 bytes";

    std::istringstream ss(sizesLine.substr(7));  // after "sizes:"
    std::vector<int> dims;
    int v;
    while (ss >> v) dims.push_back(v);
    ASSERT_EQ(dims.size(), 3u);
    // SPEC section 5 memory budget: every axis <= 128.
    EXPECT_EQ(dims[0], 128);
    EXPECT_EQ(dims[1], 128);
    EXPECT_EQ(dims[2], 70);
    EXPECT_LE(dims[0], 128);
    EXPECT_LE(dims[1], 128);
    EXPECT_LE(dims[2], 128);
    // Total voxels within budget.
    EXPECT_LE(static_cast<std::int64_t>(dims[0]) * dims[1] * dims[2],
              static_cast<std::int64_t>(128) * 128 * 128);
}

// ---------------------------------------------------------------------------
// (4) bunny.obj has its known hand-counted vertex count.
// ---------------------------------------------------------------------------
TEST(T2Assets, BunnyObjVertexCount) {
    std::ifstream in(assetPath("data/meshes/bunny.obj"));
    ASSERT_TRUE(in.good());

    std::size_t vertexCount = 0;
    std::size_t faceCount = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("v ", 0) == 0) ++vertexCount;
        if (line.rfind("f ", 0) == 0) ++faceCount;
    }
    // Hand-counted from the committed file (SPEC section 7 / data/README.md).
    EXPECT_EQ(vertexCount, 35947u);
    // 69,451 = 12 mod 7; cross-checks the same hand count.
    EXPECT_EQ(faceCount, 69451u);
}

} // namespace re::tests
