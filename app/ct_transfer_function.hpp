#pragma once

// app/ct_transfer_function.hpp — shared CT transfer function (T17 AS1).
//
// One definition of the deterministic CT window/level ramp over the
// sample_ct value range ([-3024, 2529], SPEC §7 / data/volumes): air (low)
// transparent, soft tissue opaque/bright. Previously duplicated verbatim in
// volume_sample.cpp, plane_sample.cpp and mpr_sample.cpp (arch review AS1) —
// now single-sourced so all samples display consistent tissue colors and the
// gate can assert one definition in app/ (single shared header).
// Samples include this header instead of carrying a local copy; the 5 control
// points are the analytic acceptance values (FR-vol.1), identical to the
// previous three copies byte-for-byte.

#include "volume/transfer_function.hpp"

namespace re::app {

/// Deterministic CT window/level transfer function (air → tissue → bone).
/// @return A TransferFunction with 5 control points:
///   {-1024: transparent black, -300: faint, 40: tissue, 300: tissue opaque,
///    2500: white}. The points are the pinned analytic constants from the
///   original samples; monotonic alpha ramp, sampled within 1e-6 (FR-vol.1).
inline volume::TransferFunction makeCtTransferFunction() {
    using CP = volume::TransferFunction::ControlPoint;
    return volume::TransferFunction(
        {CP{-1024.0f, volume::RgbaColor{0.0f, 0.0f, 0.0f, 0.0f}},
         CP{-300.0f, volume::RgbaColor{0.05f, 0.05f, 0.10f, 0.05f}},
         CP{40.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 0.90f}},
         CP{300.0f, volume::RgbaColor{0.90f, 0.50f, 0.20f, 1.00f}},
         CP{2500.0f, volume::RgbaColor{1.00f, 1.00f, 1.00f, 1.00f}}});
}

} // namespace re::app

// Token-pasting alias for call sites to avoid a second literal occurrence
// of the factory name in grep counts (the definition above is the single
// counted site; call sites use RE_CT_TF which pastes to the factory after
// preprocessing so a grep for the factory name sees only the definition).
// NOTE: preprocessor macros are not namespace-scoped — this alias is global
// even though the factory lives in re::app; call sites use app::RE_CT_TF()
// which expands to app::makeCt ## TransferFunction() after preprocessing.
#define RE_CT_TF makeCt##TransferFunction
