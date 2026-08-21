// io/volume/nrrd_volume_loader.cpp — NRRD volume loader implementation
// (FR-io.2, FR-io.4).

#include "io/volume/nrrd_volume_loader.hpp"

#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace re::io {
namespace {

// v1 memory budget cap (SPEC S5): every axis <= kMaxAxis and the voxel count
// <= 128^3, so the float32 storage stays <= 8 MiB. Enforced at load time so an
// oversized file fails with a typed error instead of a large allocation.
constexpr std::uint32_t kMaxAxis = 128;
constexpr std::uint64_t kMaxVoxels = 128ULL * 128ULL * 128ULL;

// Element layout for one supported scalar type (from the NRRD "type:" field).
struct ScalarInfo {
    int width;     ///< Bytes per element (1, 2, 4 or 8).
    bool isFloat;  ///< True for "float"/"double" (bit-pattern decode).
    bool isSigned; ///< True for signed integer types.
};

// The supported scalar types, mirroring tools/convert_nrrd.py's TYPE_MAP (the
// canonical format the committed NRRDs are written in, SPEC S7).
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
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return data::makeError<data::VolumeDataset>(
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
                static_cast<int>(VolumeLoadError::MalformedHeader),
                "NRRD loader: '" + path + "': header line '" + line +
                    "' is not a 'key: value' field");
        }
        const std::string key = trimmed(line.substr(0, colon));
        const std::string value = trimmed(line.substr(colon + 1));
        if (key.empty()) {
            return data::makeError<data::VolumeDataset>(
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
            static_cast<int>(VolumeLoadError::MalformedHeader),
            "NRRD loader: '" + path +
                "': missing required header field(s) (need 'dimension', "
                "'sizes', 'type')");
    }

    // --- dimension (v1: 3 only) ---------------------------------------------
    std::uint64_t dimensionValue = 0;
    if (!parseUint(*dimension, dimensionValue) || dimensionValue != 3) {
        return data::makeError<data::VolumeDataset>(
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
                static_cast<int>(VolumeLoadError::MalformedHeader),
                "NRRD loader: '" + path +
                    "': 'sizes' must be exactly three "
                    "positive integers, got '" +
                    *sizes + "'");
        }
    }

    // --- v1 memory budget cap (SPEC S5): every axis <= 128, count <= 128^3 --
    const std::uint64_t voxelCount =
        sizesValue[0] * sizesValue[1] * sizesValue[2];
    if (sizesValue[0] > kMaxAxis || sizesValue[1] > kMaxAxis ||
        sizesValue[2] > kMaxAxis || voxelCount > kMaxVoxels) {
        return data::makeError<data::VolumeDataset>(
            static_cast<int>(VolumeLoadError::BudgetExceeded),
            "NRRD loader: '" + path + "': dimensions " + *sizes +
                " exceed the v1 memory budget cap (<= 128^3, SPEC S5)");
    }

    // --- type ----------------------------------------------------------------
    const ScalarInfo* info = scalarInfo(*type);
    if (info == nullptr) {
        return data::makeError<data::VolumeDataset>(
            static_cast<int>(VolumeLoadError::UnsupportedType),
            "NRRD loader: '" + path + "': unsupported type '" + *type +
                "' (v1 supports int8..int64, uint8..uint64, float, double)");
    }

    // --- encoding (v1: raw only, SPEC S7) ------------------------------------
    const std::string* encoding = field("encoding");
    const std::string encodingValue =
        encoding == nullptr ? std::string("raw") : *encoding; // NRRD default.
    if (encodingValue != "raw") {
        return data::makeError<data::VolumeDataset>(
            static_cast<int>(VolumeLoadError::UnsupportedEncoding),
            "NRRD loader: '" + path + "': encoding '" + encodingValue +
                "' is not supported (v1 loads uncompressed 'raw' blocks only, "
                "SPEC S7)");
    }

    // --- endian (default little, per the NRRD spec) --------------------------
    const std::string* endian = field("endian");
    const std::string endianValue =
        endian == nullptr ? std::string("little") : *endian;
    const bool littleEndian = endianValue == "little";
    if (!littleEndian && endianValue != "big") {
        return data::makeError<data::VolumeDataset>(
            static_cast<int>(VolumeLoadError::MalformedHeader),
            "NRRD loader: '" + path + "': endian '" + endianValue +
                "' is invalid (expected 'little' or 'big')");
    }

    // --- raw voxel block -----------------------------------------------------
    const std::size_t expected = static_cast<std::size_t>(voxelCount) *
                                 static_cast<std::size_t>(info->width);
    const std::size_t rawSize = bytes.size() - pos;
    if (rawSize < expected) {
        return data::makeError<data::VolumeDataset>(
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
