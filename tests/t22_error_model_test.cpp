// tests/t22_error_model_test.cpp — T22 gate (error-model hardening).
//
// Review finding B8: all three io/ loaders open their error enums at
// FileOpen == 1 (ImageLoadError 1..3, MeshLoadError 1..6, VolumeLoadError
// 1..8), so inside the single `int code` carried by data::Error the same
// numeric value meant different failures depending on which loader produced
// it — disambiguation required parsing message strings. T22 makes the
// disambiguation structural and hardens Result accessors:
//
//   (1) Every loader error carries its producer domain: the SAME numeric
//       code 1 (FileOpen) from the image/mesh/volume loaders is tagged
//       ErrorDomain::ImageIo == 1 / MeshIo == 2 / VolumeIo == 3
//       respectively (the documented API numbering in data/result.hpp), so
//       consumers branch on the (domain, code) pair, never on messages.
//   (2) Codes are unchanged WITHIN each domain (regression lock R3): the
//       existing numeric assertions in t4_io_data_test.cpp /
//       t5_volume_test.cpp keep asserting the same values — proven here by
//       asserting FileOpen==1, Decode/BadMagic/VertexParse==2,
//       InvalidChannels==3 alongside the new domain tags.
//   (3) Dereferencing a failed Result asserts in debug builds (abort via
//       assert(), never an exception) — death-tested here; release builds
//       keep the documented UB. operator-> carries the same contract.
//   (4) The dead accessor hasValue() (could never differ from ok(): the
//       value branch is populated exactly when the tag is ValueTag) is
//       gone: `grep -c hasValue data/result.hpp` == 0.
//   (5) Monadic map()/andThen() (T22 stretch) preserve errors verbatim
//       (same domain/code/message) and never invoke the callable on the
//       failure path; the void-chain mirrors mpr_sample.cpp's
//       sync → renderAll → presentAll sequence exactly (stage counters).
//
// Evidence rule (R4): every expected value is either the documented enum
// numbering itself, the stable loader-code constants already asserted by
// prior gates, or an analytic constant from the monadic contracts (e.g.
// 21 * 2 == 42) — no "non-empty/>0" assertions.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "data/result.hpp"
#include "io/image/image_loader.hpp"
#include "io/mesh/obj_mesh_loader.hpp"
#include "io/volume/nrrd_volume_loader.hpp"

namespace re::tests {
namespace {

// Repo-root-relative path resolution (tests run from the build dir).
std::string assetPath(const std::string& rel) {
    return std::string(TEST_SOURCE_DIR) + "/" + rel;
}

/// Write `contents` to a uniquely-named scratch file under the system temp
/// dir (`tag` keeps concurrent fixtures distinct) and return its path.
std::filesystem::path writeTempFile(const std::string& tag,
                                    const std::string& contents) {
    auto path = std::filesystem::temp_directory_path() /
                ("re_t22_fixture_" + tag + ".tmp");
    std::ofstream out(path);
    out << contents;
    return path;
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

int countOccurrences(const std::string& hay, const std::string& needle) {
    int c = 0;
    size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++c;
        pos += needle.size();
    }
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) The documented ErrorDomain numbering is pinned API.
// ---------------------------------------------------------------------------

TEST(T22ErrorModel, DomainEnumValuesAreTheDocumentedApiNumbers) {
    // data/result.hpp declares these values explicitly; pinning them here so
    // a silent renumber cannot break persisted logs or consumer switches.
    EXPECT_EQ(static_cast<std::int32_t>(data::ErrorDomain::None), 0);
    EXPECT_EQ(static_cast<std::int32_t>(data::ErrorDomain::ImageIo), 1);
    EXPECT_EQ(static_cast<std::int32_t>(data::ErrorDomain::MeshIo), 2);
    EXPECT_EQ(static_cast<std::int32_t>(data::ErrorDomain::VolumeIo), 3);
    EXPECT_EQ(static_cast<std::int32_t>(data::ErrorDomain::Shader), 4);
    EXPECT_EQ(static_cast<std::int32_t>(data::ErrorDomain::Core), 5);
    EXPECT_EQ(static_cast<std::int32_t>(data::ErrorDomain::Utils), 6);
    EXPECT_EQ(static_cast<std::int32_t>(data::ErrorDomain::Render), 7);
    EXPECT_EQ(static_cast<std::int32_t>(data::ErrorDomain::Broker), 8);
    EXPECT_EQ(static_cast<std::int32_t>(data::ErrorDomain::Scene), 9);
}

// ---------------------------------------------------------------------------
// (2) THE gate: identical numeric codes from the three io/ loaders are
//     structurally distinguishable via the domain tag.
// ---------------------------------------------------------------------------

TEST(T22ErrorModel, SameNumericCodeFromEachLoaderIsDistinguishableByDomain) {
    // All three enums open at FileOpen == 1 (image_loader.hpp,
    // obj_mesh_loader.hpp, nrrd_volume_loader.hpp) — the colliding code.
    const std::string missing = assetPath("data/fixtures/re_t22_no_such_file");

    const auto img = io::loadImage(missing);
    ASSERT_TRUE(img.failed());
    EXPECT_EQ(img.error().code, static_cast<int>(io::ImageLoadError::FileOpen));
    EXPECT_EQ(img.error().code, 1); // the colliding numeric value
    EXPECT_EQ(img.error().domain, data::ErrorDomain::ImageIo);

    const auto mesh = io::loadObjMesh(missing);
    ASSERT_TRUE(mesh.failed());
    EXPECT_EQ(mesh.error().code, static_cast<int>(io::MeshLoadError::FileOpen));
    EXPECT_EQ(mesh.error().code, 1); // SAME number, different failure source
    EXPECT_EQ(mesh.error().domain, data::ErrorDomain::MeshIo);

    const auto vol = io::loadNrrdVolume(missing);
    ASSERT_TRUE(vol.failed());
    EXPECT_EQ(vol.error().code,
              static_cast<int>(io::VolumeLoadError::FileOpen));
    EXPECT_EQ(vol.error().code, 1); // SAME number again
    EXPECT_EQ(vol.error().domain, data::ErrorDomain::VolumeIo);

    // Structural disambiguation: the domains differ pairwise while the codes
    // are all equal — a consumer can route by (domain, code) alone.
    EXPECT_NE(img.error().domain, mesh.error().domain);
    EXPECT_NE(img.error().domain, vol.error().domain);
    EXPECT_NE(mesh.error().domain, vol.error().domain);
    EXPECT_EQ(img.error().code, mesh.error().code);
    EXPECT_EQ(mesh.error().code, vol.error().code);
}

TEST(T22ErrorModel, NonOpenErrorsKeepTheirCodesWithinTheirDomain) {
    // Regression lock (R3): codes are unchanged within each domain — the
    // same constants t4_io_data_test.cpp / t5_volume_test.cpp assert, now
    // additionally stamped with the producer domain.

    // Garbage bytes → NRRD BadMagic == 2, domain VolumeIo.
    const auto badMagic = writeTempFile("badmagic", "hello world\n");
    const auto vol = io::loadNrrdVolume(badMagic.string());
    ASSERT_TRUE(vol.failed());
    EXPECT_EQ(vol.error().code, static_cast<int>(io::VolumeLoadError::BadMagic));
    EXPECT_EQ(vol.error().code, 2);
    EXPECT_EQ(vol.error().domain, data::ErrorDomain::VolumeIo);

    // Two-float vertex line → OBJ VertexParse == 2, domain MeshIo.
    const auto badVertex = writeTempFile("badvertex", "v 1.0 2.0\n");
    const auto mesh = io::loadObjMesh(badVertex.string());
    ASSERT_TRUE(mesh.failed());
    EXPECT_EQ(mesh.error().code,
              static_cast<int>(io::MeshLoadError::VertexParse));
    EXPECT_EQ(mesh.error().code, 2);
    EXPECT_EQ(mesh.error().domain, data::ErrorDomain::MeshIo);

    // requestedChannels = 7 rejected before any IO → InvalidChannels == 3,
    // domain ImageIo (argument validation, not file access).
    const auto img =
        io::loadImage(assetPath("data/fixtures/golden_image.png"), 7);
    ASSERT_TRUE(img.failed());
    EXPECT_EQ(img.error().code,
              static_cast<int>(io::ImageLoadError::InvalidChannels));
    EXPECT_EQ(img.error().code, 3);
    EXPECT_EQ(img.error().domain, data::ErrorDomain::ImageIo);
}

// ---------------------------------------------------------------------------
// (3) Ok-result accessors unchanged. The failed-deref debug trap is
//     death-tested in tests/t22_debug_assert_death_test.cpp — a dedicated
//     binary WITHOUT the offscreen GL fixture, because gtest death tests
//     fork(), and forking the fixture's multithreaded llvmpipe context is
//     slow (~10 s per assertion) and documented-unsafe; assert()/abort()
//     itself needs nothing but data/result.hpp.
// ---------------------------------------------------------------------------

TEST(T22ErrorModel, OkResultAccessorsStillWork) {
    auto r = data::makeValue<int>(21);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(*r, 21); // direct dereference of a held value
    *r = 42;           // mutable overload writes through
    EXPECT_EQ(*r, 42);

    const auto& cr = r;
    EXPECT_EQ(cr.operator->(), &*cr); // pointer aliases the stored value

    auto s = data::makeValue<std::string>("abc");
    ASSERT_TRUE(s.ok());
    EXPECT_EQ(s->size(), 3u); // operator-> member access on ok()
}

// ---------------------------------------------------------------------------
// (4) Dead accessor removed (mechanical grep floor, t17 pattern).
// ---------------------------------------------------------------------------

TEST(T22ErrorModel, HasValueAccessorIsFullyRemoved) {
    // hasValue() could never differ from ok() (the optional is engaged
    // exactly when the branch is the value branch), so every call site was
    // dead weight. The header must contain zero occurrences — comment prose
    // included — so the API cannot quietly return.
    const std::string header =
        readFile(std::filesystem::path(TEST_SOURCE_DIR) / "data" /
                 "result.hpp");
    ASSERT_FALSE(header.empty())
        << "data/result.hpp must be readable from the source root";
    EXPECT_EQ(countOccurrences(header, "hasValue"), 0)
        << "data/result.hpp must no longer mention hasValue (T22 removal)";
}

// ---------------------------------------------------------------------------
// (5) Factory round-trips: domain-tagged and legacy forms.
// ---------------------------------------------------------------------------

TEST(T22ErrorModel, DomainTaggedFactoryRoundTrip) {
    const auto r = data::makeError<double>(data::ErrorDomain::Broker, 8,
                                           "asset store: stale handle");
    ASSERT_TRUE(r.failed());
    EXPECT_EQ(r.error().domain, data::ErrorDomain::Broker);
    EXPECT_EQ(r.error().code, 8);
    EXPECT_EQ(r.error().message, "asset store: stale handle");

    // The legacy two-argument form stays valid for untagged producers and
    // yields the documented None domain.
    const auto legacy = data::makeError<double>(1, "legacy ad-hoc site");
    ASSERT_TRUE(legacy.failed());
    EXPECT_EQ(legacy.error().domain, data::ErrorDomain::None);
    EXPECT_EQ(legacy.error().code, 1);
}

// ---------------------------------------------------------------------------
// (6) Monadic helpers (T22 stretch): error propagation is verbatim, the
//     callable runs only on the success path.
// ---------------------------------------------------------------------------

TEST(T22ErrorModel, MapTransformsHeldValueAnalytically) {
    auto mapped = data::makeValue<int>(21).map([](int v) { return v * 2; });
    ASSERT_TRUE(mapped.ok());
    EXPECT_EQ(*mapped, 42); // 21 * 2 — closed form
}

TEST(T22ErrorModel, MapOnFailureSkipsFnAndPropagatesErrorVerbatim) {
    data::Result<int> r(data::error,
                        data::Error{data::ErrorDomain::Scene, 2,
                                    "store: stale handle after erase"});
    int fnCalls = 0;
    auto mapped = std::move(r).map([&fnCalls](int v) {
        ++fnCalls;
        return v;
    });
    ASSERT_TRUE(mapped.failed());
    EXPECT_EQ(fnCalls, 0) << "map must not invoke fn on the failure path";
    EXPECT_EQ(mapped.error().domain, data::ErrorDomain::Scene);
    EXPECT_EQ(mapped.error().code, 2);
    EXPECT_EQ(mapped.error().message, "store: stale handle after erase");
}

TEST(T22ErrorModel, AndThenChainsSuccessesAndSurfacesStageFailures) {
    auto bothOk = data::makeValue<int>(5).andThen([](int v) {
        return data::makeValue<int>(v * v); // 5^2 = 25
    });
    ASSERT_TRUE(bothOk.ok());
    EXPECT_EQ(*bothOk, 25);

    auto secondFails = data::makeValue<int>(5).andThen([](int) {
        return data::makeError<int>(data::ErrorDomain::Render, 1,
                                    "ViewTarget: invalid size 0");
    });
    ASSERT_TRUE(secondFails.failed());
    EXPECT_EQ(secondFails.error().domain, data::ErrorDomain::Render);
    EXPECT_EQ(secondFails.error().code, 1);
    EXPECT_EQ(secondFails.error().message, "ViewTarget: invalid size 0");
}

TEST(T22ErrorModel, AndThenShortCircuitsOnFirstFailureVerbatim) {
    data::Result<int> r(data::error,
                        data::Error{data::ErrorDomain::ImageIo, 3,
                                    "image loader: invalid requestedChannels=7"});
    int fnCalls = 0;
    auto chained = std::move(r).andThen([&fnCalls](int) {
        ++fnCalls;
        return data::makeValue<int>(0);
    });
    ASSERT_TRUE(chained.failed());
    EXPECT_EQ(fnCalls, 0) << "andThen must short-circuit on the failure path";
    EXPECT_EQ(chained.error().domain, data::ErrorDomain::ImageIo);
    EXPECT_EQ(chained.error().code, 3);
}

// ---------------------------------------------------------------------------
// (7) The void-chain shape used by app/mpr_sample.cpp renderFrame:
//     sync → renderAll → presentAll, first failure short-circuits the rest.
//     Stage codes 10/11/12 mirror the synchronizer's ensureView convention
//     (ViewSynchronizer reports code 10 for sync-stage failures).
// ---------------------------------------------------------------------------

namespace {

struct StageLog {
    int syncCalls = 0;
    int renderCalls = 0;
    int presentCalls = 0;

    data::Result<void> sync(bool fail) {
        ++syncCalls;
        if (fail) {
            return data::makeError<void>(data::ErrorDomain::Broker, 10,
                                         "sync stage failed");
        }
        return data::Result<void>(data::value);
    }
    data::Result<void> render(bool fail) {
        ++renderCalls;
        if (fail) {
            return data::makeError<void>(data::ErrorDomain::Render, 11,
                                         "render stage failed");
        }
        return data::Result<void>(data::value);
    }
    data::Result<void> present(bool fail) {
        ++presentCalls;
        if (fail) {
            return data::makeError<void>(data::ErrorDomain::Core, 12,
                                         "present stage failed");
        }
        return data::Result<void>(data::value);
    }

    /// Exactly the mpr_sample.cpp renderFrame expression.
    data::Result<void> run(bool syncFails, bool renderFails, bool presFails) {
        return sync(syncFails)
            .andThen([this, renderFails] { return render(renderFails); })
            .andThen([this, presFails] { return present(presFails); });
    }
};

} // namespace

TEST(T22ErrorModel, VoidChainRunsAllStagesWhenAllSucceed) {
    StageLog log;
    const auto r = log.run(false, false, false);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(log.syncCalls, 1);
    EXPECT_EQ(log.renderCalls, 1);
    EXPECT_EQ(log.presentCalls, 1);
}

TEST(T22ErrorModel, VoidChainStopsAtFirstFailedStageWithItsError) {
    // Sync fails: later stages must be skipped entirely, error verbatim.
    StageLog syncFail;
    const auto r1 = syncFail.run(true, true, true);
    ASSERT_TRUE(r1.failed());
    EXPECT_EQ(r1.error().domain, data::ErrorDomain::Broker);
    EXPECT_EQ(r1.error().code, 10);
    EXPECT_EQ(syncFail.syncCalls, 1);
    EXPECT_EQ(syncFail.renderCalls, 0);
    EXPECT_EQ(syncFail.presentCalls, 0);

    // Render fails: sync ran once, present skipped, render's error surfaces.
    StageLog renderFail;
    const auto r2 = renderFail.run(false, true, true);
    ASSERT_TRUE(r2.failed());
    EXPECT_EQ(r2.error().domain, data::ErrorDomain::Render);
    EXPECT_EQ(r2.error().code, 11);
    EXPECT_EQ(renderFail.syncCalls, 1);
    EXPECT_EQ(renderFail.renderCalls, 1);
    EXPECT_EQ(renderFail.presentCalls, 0);

    // Present fails: both earlier stages ran once, present's error surfaces.
    StageLog presentFail;
    const auto r3 = presentFail.run(false, false, true);
    ASSERT_TRUE(r3.failed());
    EXPECT_EQ(r3.error().domain, data::ErrorDomain::Core);
    EXPECT_EQ(r3.error().code, 12);
    EXPECT_EQ(presentFail.syncCalls, 1);
    EXPECT_EQ(presentFail.renderCalls, 1);
    EXPECT_EQ(presentFail.presentCalls, 1);
}

} // namespace re::tests
