// io/volume/nrrd_volume_loader.cpp — NRRD volume loader implementation
// (FR-io.2, FR-io.4).

#include "io/volume/nrrd_volume_loader.hpp"

#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/caps.hpp"

namespace re::io {
namespace {

// No cap streaming via core::Caps — any dims tiled/downsampled (SPEC §7 FR-io.2,
// T11). The committed sample_ct.nrrd is example 128×128×70 ≤128³, but product
// loader has no ≤128³ cap: any dims load and the renderer tiles via
// core::Caps maxTexture3DSize (1/255 within reference). The host file-size
// pre-probe guards the whole-file slurp against hostile multi-gigabyte files
// without a hard dims window: it probes file size before slurp and compares
// against a large derived ceiling (512³ * 8 + 64 KiB header slack) so a 256³
// synthetic volume (16 MB uint8, 64 MB float, 134 MB double) passes while a
// multi-GB hostile file fails fast with a typed error and warn log.
constexpr std::uint64_t kHeaderSlackBytes = 64ULL * 1024ULL;
constexpr std::uint64_t kMaxFileBytes = 512ULL * 512ULL * 512ULL * 8ULL + kHeaderSlackBytes;

inline data::Result<data::VolumeDataset> makeBudgetError(const std::string& path,
                                                          const std::string& detail) {
    return data::makeError<data::VolumeDataset>(
        data::ErrorDomain::VolumeIo,
        static_cast<int>(VolumeLoadError::BudgetExceeded),
        "NRRD loader: '" + path + "': " + detail);
}

// Element layout for one supported scalar type (from the NRRD "type:" field).
struct ScalarInfo {
    int width;     ///< Bytes per element (1, 2, 4 or 8).
    bool isFloat;  ///< True for "float"/"double" (bit-pattern decode).
    bool isSigned; ///< True for signed integer types.
};

// The supported scalar types, mirroring tools/convert_nrrd.py's TYPE_MAP (the
// canonical writer for the committed NRRD datasets): the loader accepts
// exactly the element types that tool emits, so a dataset converted once is
// always loadable here and vice versa.
const ScalarInfo* scalarInfo(const std::string& type) {
    static const struct {
        const char* name;
        ScalarInfo info;
    } kTypes[] = {
        {"int8", {1, false, true}},    {"uint8", {1, false, false}},
        {"int16", {2, false, true}},   {"uint16", {2, false, false}},
        {"int32", {4, false, true}},   {"int", {4, false, true}},
        {"uint32", {4, false, false}}, {"int64", {8, false, true}},
        {"uint64", {8, false, false}}, {"float", {4, true, false}},
        {"double", {8, true, false}},
    };
    for (const auto& t : kTypes) {
        if (type == t.name) {
            return &t.info;
        }
    }
    return nullptr;
}

std::string trimmed(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Parse a strictly-positive unsigned integer from `text` (the whole string
// must be consumed; "3.0", "-1", "" all fail).
bool parseUint(const std::string& text, std::uint64_t& out) {
    if (text.empty()) {
        return false;
    }
    std::uint64_t v = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), v);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    if (v == 0) {
        return false; // NRRD sizes are positive.
    }
    out = v;
    return true;
}

// Decode one element of the raw block: assemble `width` bytes into an integer
// bit pattern honoring the file's endianness, then convert to float (integer
// types sign-extend; float/double reinterpret the bits).
float decodeElement(const std::uint8_t* bytes, const ScalarInfo& info,
                    bool littleEndian) {
    std::uint64_t bits = 0;
    if (littleEndian) {
        for (int k = 0; k < info.width; ++k) {
            bits |= static_cast<std::uint64_t>(bytes[k]) << (8 * k);
        }
    } else {
        for (int k = 0; k < info.width; ++k) {
            bits |= static_cast<std::uint64_t>(bytes[k])
                    << (8 * (info.width - 1 - k));
        }
    }

    if (info.isFloat) {
        if (info.width == 4) {
            const std::uint32_t u = static_cast<std::uint32_t>(bits);
            return std::bit_cast<float>(u);
        }
        return static_cast<float>(std::bit_cast<double>(bits));
    }

    if (info.isSigned) {
        switch (info.width) {
            case 1:
                return static_cast<float>(
                    static_cast<std::int8_t>(static_cast<std::uint8_t>(bits)));
            case 2:
                return static_cast<float>(static_cast<std::int16_t>(
                    static_cast<std::uint16_t>(bits)));
            case 4:
                return static_cast<float>(static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(bits)));
            case 8:
                return static_cast<float>(static_cast<std::int64_t>(bits));
        }
    } else {
        switch (info.width) {
            case 1:
                return static_cast<float>(static_cast<std::uint8_t>(bits));
            case 2:
                return static_cast<float>(static_cast<std::uint16_t>(bits));
            case 4:
                return static_cast<float>(static_cast<std::uint32_t>(bits));
            case 8:
                return static_cast<float>(static_cast<std::uint64_t>(bits));
        }
    }
    return 0.0f; // Unreachable: width is always 1/2/4/8 from scalarInfo().
}

} // namespace

data::Result<data::VolumeDataset> loadNrrdVolume(const std::string& path) {
    // Host file-size pre-probe before any allocation. The check uses the
    // derived ceiling (worst-case raw block 512^3 * 8 for double/int64) plus
    // header slack as absolute cap, so a hostile multi-gigabyte file fails
    // fast without a multi-megabyte slurp, while valid volumes (including
    // 256³ synthetic, ~16 MB uint8 / 64 MB float) pass unchanged; this is
    // the sole budget guard (No cap streaming per T11 — dims no longer cap).
    {
        std::error_code ec;
        const auto fileSize = std::filesystem::file_size(path, ec);
        if (!ec) {
            if (static_cast<std::uint64_t>(fileSize) > kMaxFileBytes) {
                spdlog::warn(
                    "NRRD loader: '{}' file size {} bytes exceeds absolute "
                    "cap {} bytes (derived ceiling 512^3 * 8 + {} header slack) "
                    "— rejecting before slurp to avoid OOM",
                    path, static_cast<std::uint64_t>(fileSize), kMaxFileBytes,
                    kHeaderSlackBytes);
                return makeBudgetError(path, "file size " + std::to_string(static_cast<std::uint64_t>(fileSize)) + " bytes exceeds the absolute cap " + std::to_string(kMaxFileBytes) + " bytes (512^3 * 8 + header slack)");
            }
        }
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return data::makeError<data::VolumeDataset>(
            data::ErrorDomain::VolumeIo,
            static_cast<int>(VolumeLoadError::FileOpen),
            "NRRD loader: cannot open file '" + path + "'");
    }
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                          std::istreambuf_iterator<char>()};

    // --- Header: lines until the first empty line (LF or CRLF files) -------
    std::size_t pos = 0;
    auto nextLine = [&](std::string& line) -> bool {
        if (pos >= bytes.size()) {
            return false;
        }
        std::size_t nl = bytes.size();
        for (std::size_t i = pos; i < bytes.size(); ++i) {
            if (bytes[i] == '\n') {
                nl = i;
                break;
            }
        }
        line.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                    bytes.begin() + static_cast<std::ptrdiff_t>(nl));
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        pos = (nl < bytes.size()) ? nl + 1 : bytes.size();
        return true;
    };

    std::string magic;
    if (!nextLine(magic)) {
        return data::makeError<data::VolumeDataset>(
            data::ErrorDomain::VolumeIo,
            static_cast<int>(VolumeLoadError::BadMagic),
            "NRRD loader: '" + path + "' is empty, not a NRRD file");
    }
    // Magic is "NRRD000" followed by a digit 1..5 (the committed files are
    // NRRD0004).
    const bool magicOk = magic.size() == 8 &&
                         magic.compare(0, 7, "NRRD000") == 0 &&
                         magic[7] >= '1' && magic[7] <= '5';
    if (!magicOk) {
        return data::makeError<data::VolumeDataset>(
            data::ErrorDomain::VolumeIo,
            static_cast<int>(VolumeLoadError::BadMagic),
            "NRRD loader: '" + path + "' first line '" + magic +
                "' is not a NRRD magic line (expected NRRD0001..NRRD0005)");
    }

    // Field lines: "key: value"; a line starting with whitespace continues the
    // previous field; "#" lines are comments. The first empty line terminates
    // the header (anything after is the raw voxel block).
    std::vector<std::string> fieldKeys;
    std::vector<std::string> fieldValues;
    bool headerTerminated = false;
    std::string line;
    while (nextLine(line)) {
        if (line.empty()) {
            headerTerminated = true; // pos now points at the raw block.
            break;
        }
        if (line[0] == '#') {
            continue; // Comment line.
        }
        if (line[0] == ' ' || line[0] == '\t') {
            if (fieldKeys.empty()) {
                return data::makeError<data::VolumeDataset>(
                    data::ErrorDomain::VolumeIo,
                    static_cast<int>(VolumeLoadError::MalformedHeader),
                    "NRRD loader: '" + path +
                        "': continuation line before any field");
            }
            fieldValues.back() += " " + trimmed(line);
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            return data::makeError<data::VolumeDataset>(
                data::ErrorDomain::VolumeIo,
                static_cast<int>(VolumeLoadError::MalformedHeader),
                "NRRD loader: '" + path + "': header line '" + line +
                    "' is not a 'key: value' field");
        }
        const std::string key = trimmed(line.substr(0, colon));
        const std::string value = trimmed(line.substr(colon + 1));
        if (key.empty()) {
            return data::makeError<data::VolumeDataset>(
                data::ErrorDomain::VolumeIo,
                static_cast<int>(VolumeLoadError::MalformedHeader),
                "NRRD loader: '" + path + "': header line '" + line +
                    "' has an empty field key");
        }
        fieldKeys.push_back(key);
        fieldValues.push_back(value);
    }
    if (!headerTerminated) {
        // Reached EOF without a blank header-terminator line.
        return data::makeError<data::VolumeDataset>(
            data::ErrorDomain::VolumeIo,
            static_cast<int>(VolumeLoadError::MalformedHeader),
            "NRRD loader: '" + path +
                "': header is not terminated by a blank line");
    }

    // --- Required fields ----------------------------------------------------
    auto field = [&](const std::string& key) -> const std::string* {
        for (std::size_t i = 0; i < fieldKeys.size(); ++i) {
            if (fieldKeys[i] == key) {
                return &fieldValues[i];
            }
        }
        return nullptr;
    };

    const std::string* dimension = field("dimension");
    const std::string* sizes = field("sizes");
    const std::string* type = field("type");
    if (dimension == nullptr || sizes == nullptr || type == nullptr) {
        return data::makeError<data::VolumeDataset>(
            data::ErrorDomain::VolumeIo,
            static_cast<int>(VolumeLoadError::MalformedHeader),
            "NRRD loader: '" + path +
                "': missing required header field(s) (need 'dimension', "
                "'sizes', 'type')");
    }

    // --- dimension (v1: 3 only) ---------------------------------------------
    std::uint64_t dimensionValue = 0;
    if (!parseUint(*dimension, dimensionValue) || dimensionValue != 3) {
        return data::makeError<data::VolumeDataset>(
            data::ErrorDomain::VolumeIo,
            static_cast<int>(VolumeLoadError::UnsupportedDimension),
            "NRRD loader: '" + path + "': dimension '" + *dimension +
                "' is not supported (v1 loads 3D volumes only)");
    }

    // --- sizes (exactly 3 positive integers, x y z) --------------------------
    std::uint64_t sizesValue[3] = {0, 0, 0};
    {
        std::size_t tokenCount = 0;
        std::size_t i = 0;
        while (i <= sizes->size()) {
            const std::size_t end = sizes->find_first_of(" \t", i);
            const std::string token = trimmed(sizes->substr(
                i, end == std::string::npos ? std::string::npos : end - i));
            if (!token.empty()) {
                if (tokenCount >= 3 ||
                    !parseUint(token, sizesValue[tokenCount])) {
                    return data::makeError<data::VolumeDataset>(
                        data::ErrorDomain::VolumeIo,
                        static_cast<int>(VolumeLoadError::MalformedHeader),
                        "NRRD loader: '" + path +
                            "': 'sizes' must be exactly "
                            "three positive integers, got '" +
                            *sizes + "'");
                }
                ++tokenCount;
            }
            if (end == std::string::npos) {
                break;
            }
            i = end + 1;
        }
        if (tokenCount != 3) {
            return data::makeError<data::VolumeDataset>(
                data::ErrorDomain::VolumeIo,
                static_cast<int>(VolumeLoadError::MalformedHeader),
                "NRRD loader: '" + path +
                    "': 'sizes' must be exactly three "
                    "positive integers, got '" +
                    *sizes + "'");
        }
    }

    // --- voxel count (No cap streaming, T11) ----------------------------------
    // T11 lifts the historical 128³ dims cap: any dims are accepted here and
    // the renderer tiles/downsamples via core::Caps maxTexture3DSize (1/255
    // within reference). The loader no longer returns the budget error for
    // >128³ alone — that error only when the GL caps probe fails (in
    // render/) or the host file size exceeds the large absolute cap above.
    // Checked product with overflow guard and UINT32_MAX range-check before
    // static_cast<uint32_t>; size==0 or >UINT32_MAX triggers cap error
    // only when core::Caps probe also fails (NFR §5 tiled streaming).
    for (int i = 0; i < 3; ++i) {
        if (sizesValue[i] == 0 || sizesValue[i] > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            const auto& caps = core::caps();
            if (caps.maxTexture3DSize == 0) {
                return makeBudgetError(path, "sizes value " + std::to_string(sizesValue[i]) + " out of uint32 range or zero (Caps probe fail)");
            }
            return makeBudgetError(path, "sizes value " + std::to_string(sizesValue[i]) + " out of uint32 range or zero");
        }
    }
    std::uint64_t voxelCount = 0;
    {
        std::uint64_t tmp = 0;
        if (__builtin_mul_overflow(sizesValue[0], sizesValue[1], &tmp) ||
            __builtin_mul_overflow(tmp, sizesValue[2], &voxelCount)) {
            return makeBudgetError(path, "voxel count overflow");
        }
    }
    {
        std::uint64_t expectedBytes = 0;
        // width is set later but we can guard voxelCount * 8 (max width) against overflow
        if (__builtin_mul_overflow(voxelCount, static_cast<std::uint64_t>(8), &expectedBytes)) {
            return makeBudgetError(path, "voxel bytes overflow");
        }
        if (expectedBytes > kMaxFileBytes) {
            const auto& caps = core::caps();
            if (caps.maxTexture3DSize == 0) {
                return makeBudgetError(path, "voxel block " + std::to_string(expectedBytes) + " exceeds kMaxFileBytes and Caps probe failed");
            }
        }
    }

    // --- type ----------------------------------------------------------------
    const ScalarInfo* info = scalarInfo(*type);
    if (info == nullptr) {
        return data::makeError<data::VolumeDataset>(
            data::ErrorDomain::VolumeIo,
            static_cast<int>(VolumeLoadError::UnsupportedType),
            "NRRD loader: '" + path + "': unsupported type '" + *type +
                "' (v1 supports int8..int64, uint8..uint64, float, double)");
    }

    // --- encoding (v1: raw only) ---------------------------------------------
    // "raw" means an uncompressed voxel block; gzip/bzip2 NRRD encodings are
    // rejected with UnsupportedEncoding rather than half-supported, so a
    // successfully loaded dataset always has a plain, seekable byte layout.
    const std::string* encoding = field("encoding");
    const std::string encodingValue =
        encoding == nullptr ? std::string("raw") : *encoding; // NRRD default.
    if (encodingValue != "raw") {
        return data::makeError<data::VolumeDataset>(
            data::ErrorDomain::VolumeIo,
            static_cast<int>(VolumeLoadError::UnsupportedEncoding),
            "NRRD loader: '" + path + "': encoding '" + encodingValue +
                "' is not supported (v1 loads uncompressed 'raw' blocks only, "
                "SPEC §7)");
    }

    // --- endian (default little, per the NRRD spec) --------------------------
    const std::string* endian = field("endian");
    const std::string endianValue =
        endian == nullptr ? std::string("little") : *endian;
    const bool littleEndian = endianValue == "little";
    if (!littleEndian && endianValue != "big") {
        return data::makeError<data::VolumeDataset>(
            data::ErrorDomain::VolumeIo,
            static_cast<int>(VolumeLoadError::MalformedHeader),
            "NRRD loader: '" + path + "': endian '" + endianValue +
                "' is invalid (expected 'little' or 'big')");
    }

    // --- raw voxel block -----------------------------------------------------
    std::uint64_t expected64 = 0;
    if (__builtin_mul_overflow(voxelCount, static_cast<std::uint64_t>(info->width), &expected64)) {
        return makeBudgetError(path, "expected bytes overflow");
    }
    const std::size_t expected = static_cast<std::size_t>(expected64);
    const std::size_t rawSize = bytes.size() - pos;
    if (rawSize < expected) {
        return data::makeError<data::VolumeDataset>(
            data::ErrorDomain::VolumeIo,
            static_cast<int>(VolumeLoadError::VoxelBlockSize),
            "NRRD loader: '" + path + "': raw voxel block has " +
                std::to_string(rawSize) + " bytes but " +
                std::to_string(expected) + " are required (" +
                std::to_string(voxelCount) + " voxels x " +
                std::to_string(info->width) + " bytes each)");
    }

    // Decode every element into float32 (x-fastest order). Nothing partial can
    // escape: the VolumeDataset is constructed only once all validation above
    // has passed.
    std::vector<float> voxels;
    voxels.reserve(static_cast<std::size_t>(voxelCount));
    const std::uint8_t* base = bytes.data() + pos;
    for (std::uint64_t i = 0; i < voxelCount; ++i) {
        voxels.push_back(
            decodeElement(base + i * info->width, *info, littleEndian));
    }

    return data::makeValue<data::VolumeDataset>(data::VolumeDataset(
        static_cast<std::uint32_t>(sizesValue[0]),
        static_cast<std::uint32_t>(sizesValue[1]),
        static_cast<std::uint32_t>(sizesValue[2]), std::move(voxels)));
}

} // namespace re::io
