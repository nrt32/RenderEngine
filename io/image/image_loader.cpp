// io/image/image_loader.cpp — stb_image loader implementation (FR-io.3,
// FR-io.4).
//
// STB_IMAGE_IMPLEMENTATION is defined in exactly this translation unit; the
// stb headers themselves are public-domain (SPEC §2, pinned commit).
//
// The file is opened with std::ifstream first so that "cannot open" and
// "cannot decode" map to distinct typed errors (FR-io.4); the bytes are then
// handed to stbi_load_from_memory, whose nullptr return (with a failure
// reason) signals a malformed/undecodable file.

#include "io/image/image_loader.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace re::io {

data::Result<data::Image> loadImage(const std::string& path,
                                    std::int32_t requestedChannels) {
    if (requestedChannels < 0 || requestedChannels > 4) {
        return data::makeError<data::Image>(
            data::ErrorDomain::ImageIo,
            static_cast<int>(ImageLoadError::InvalidChannels),
            "image loader: invalid requestedChannels=" +
                std::to_string(requestedChannels) +
                " (must be 0..4; 0 keeps the file's native channel count)");
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return data::makeError<data::Image>(
            data::ErrorDomain::ImageIo,
            static_cast<int>(ImageLoadError::FileOpen),
            "image loader: cannot open file '" + path + "'");
    }
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                          std::istreambuf_iterator<char>()};

    int width = 0;
    int height = 0;
    int channels = 0;
    // stbi_load_from_memory returns nullptr (and sets stbi_failure_reason) on
    // any malformed input; on success the pixel buffer is owned by us and must
    // be released with stbi_image_free.
    stbi_uc* pixels =
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                              &width, &height, &channels, requestedChannels);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return data::makeError<data::Image>(
            data::ErrorDomain::ImageIo,
            static_cast<int>(ImageLoadError::Decode),
            std::string("image loader: cannot decode '") + path + "': " +
                (reason != nullptr ? reason : "unknown stb_image error"));
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return data::makeError<data::Image>(
            data::ErrorDomain::ImageIo,
            static_cast<int>(ImageLoadError::Decode),
            "image loader: '" + path + "' decoded to a zero-sized image");
    }

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    // stb's *channels output is ALWAYS the source image's channel count (its
    // documented contract: "n will always be the number that it would have
    // been if you said 0"), even when req_comp forces a different output
    // layout. The decoded buffer therefore has requestedChannels bytes per
    // pixel when a conversion was requested, and `channels` otherwise.
    const std::int32_t outputChannels =
        requestedChannels != 0 ? requestedChannels : channels;
    const std::size_t byteCount =
        pixelCount * static_cast<std::size_t>(outputChannels);

    std::vector<std::uint8_t> decoded(pixels, pixels + byteCount);
    stbi_image_free(pixels);

    return data::makeValue<data::Image>(
        data::Image(width, height, outputChannels, std::move(decoded)));
}

} // namespace re::io