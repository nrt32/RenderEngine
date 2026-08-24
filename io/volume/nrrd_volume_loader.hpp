#pragma once

// io/volume/nrrd_volume_loader.hpp — NRRD volume loader (FR-io.2, FR-io.4).
//
// io/ is GL-free: the loader parses a NRRD file (text header + raw,
// uncompressed voxel block, SPEC §7) into a data::VolumeDataset (CPU
// container).
//
// v1 supported subset (the format tools/convert_nrrd.py writes):
//   - magic "NRRD0001".."NRRD0005" (the committed files are NRRD0004);
//   - dimension 3 with exactly three "sizes";
//   - scalar types "int8" "uint8" "int16" "uint16" "int32" "int" (int32)
//     "uint32" "int64" "uint64" "float" "double" (the tools/convert_nrrd.py
//     TYPE_MAP);
//   - "endian: little" or "endian: big" (default little, per the NRRD spec);
//   - "encoding: raw" only (SPEC §7: raw, uncompressed voxel block; gzip/bzip2/
//     txt/hex/ascii are rejected as UnsupportedEncoding in v1);
//   - header fields beyond these (space, space directions, kinds, space origin,
//     ...) are ignored — v1 samples in index space.
//
// The voxel block must hold at least sizes-product * element-width bytes
// (extra trailing bytes are ignored, matching tools/convert_nrrd.py). The v1
// memory budget cap (SPEC §5: every axis <= 128 and the product <= 128^3) is
// enforced here, so an oversized file fails with a typed error instead of a
// multi-megabyte allocation.
//
// Errors (FR-io.4) are reported as a typed data::Result carrying a
// VolumeLoadError code and a message naming the offending file/field; no
// exceptions are thrown and no partially-built VolumeDataset ever escapes (the
// dataset is constructed only after the whole file has been validated).

#include <cstdint>
#include <string>

#include "data/result.hpp"
#include "data/volume_dataset.hpp"

namespace re::io {

/// Error codes carried by data::Error::code for NRRD load failures. Typed
/// and enumerated (never thrown): callers branch on the code instead of
/// parsing messages, and the numeric values are stable API — tests assert
/// them. Every NRRD-loader error is stamped with
/// `data::ErrorDomain::VolumeIo`, so its codes are structurally
/// distinguishable from the numerically-colliding ranges of the other io/
/// loaders (all three start at FileOpen == 1) without string parsing.
enum class VolumeLoadError : int {
    FileOpen = 1,             ///< The file could not be opened for reading.
    BadMagic = 2,             ///< The file is not a NRRD (bad magic line).
    MalformedHeader = 3,      ///< Required header fields missing/malformed.
    UnsupportedDimension = 4, ///< dimension is not 3 (v1 is 3D only).
    UnsupportedType = 5,      ///< type is not in the supported scalar set.
    UnsupportedEncoding = 6,  ///< encoding is not "raw" (v1 loads raw only).
    VoxelBlockSize = 7,       ///< Raw block shorter than dims * element size.
    BudgetExceeded = 8,       ///< Dims exceed the v1 budget cap (<= 128^3).
};

/// Load a NRRD volume from `path` into a `data::VolumeDataset` (FR-io.2).
///
/// Returns a typed error (FR-io.4) on any malformed input; a failed result
/// never carries a partial VolumeDataset. Voxel values are converted to
/// float32 (exact for the committed sample data, |v| < 2^24).
data::Result<data::VolumeDataset> loadNrrdVolume(const std::string& path);

} // namespace re::io
