#pragma once

// io/image/image_loader.hpp — image loader via stb_image (FR-io.3, FR-io.4).
//
// io/ is GL-free: the loader decodes an image file into a data::Image (CPU
// pixel container, stb's native row-major top-left origin). v1 supports any
// format stb_image handles (PNG, JPG, BMP, TGA, ...).
//
// Errors (FR-io.4) are reported as a typed data::Result carrying an
// ImageLoadError code and a message (including stb's failure reason for decode
// failures); no exceptions are thrown and no partially-decoded Image ever
// escapes.

#include <cstdint>
#include <string>

#include "data/image.hpp"
#include "data/result.hpp"

namespace re::io {

/// Enumerated diagnostics codes carried by data::Error::code for image load
/// failures (typed, SPEC S5). Public API: lives in the header.
enum class ImageLoadError : int {
    FileOpen = 1,        ///< The file could not be opened for reading.
    Decode = 2,          ///< stb_image could not decode the file.
    InvalidChannels = 3, ///< requestedChannels is not in {0, 1, 2, 3, 4}.
};

/// Load an image from `path` into a `data::Image` (FR-io.3).
///
/// `requestedChannels` selects the byte layout of the decoded pixels:
///   0 — keep the file's native channel count (e.g. 3 for an RGB PNG);
///   1..4 — force that many channels (missing channels are filled with 0,
///          alpha defaults to 255), per stb_image's conversion rules.
/// Returns a typed error (FR-io.4) on any malformed input; a failed result
/// never carries a partial Image.
data::Result<data::Image> loadImage(const std::string& path,
                                    std::int32_t requestedChannels = 0);

} // namespace re::io