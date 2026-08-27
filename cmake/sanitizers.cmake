# cmake/sanitizers.cmake — ASan+UBSan INTERFACE target (SPEC §5, T11)
#
# Defines INTERFACE target `re_project_sanitizers` that instruments all nine
# `re_*` static libs (re_core, re_data, re_io, re_volume, re_scene, re_broker,
# re_render, re_utils, re_app) — not just test/sample TUs — so intra-library
# stack/scope/intra-object errors are caught (SPEC §5 sanitizer contract).
#
# Flags: `-fsanitize=address,undefined -fno-omit-frame-pointer -O1` under Debug
# only, option-gated via `RE_ENABLE_SANITIZERS` (ON for Debug, OFF for Release
# keeps Release non-instrumented). Generator expressions gate on CONFIG:Debug so
# a Release build never sees sanitizers even when the option is ON.
#
# Every `re_*` target links this interface publicly; test and sample executables
# also link it for their own TUs. Ad-hoc per-dir `target_compile_options`
# with fsanitize blocks in tests/ and app/ are deleted — this interface is the
# sole sanitizer source (audit forbids per-target fsanitize via
# add_compile_options).
#
# Known driver suppressions for llvmpipe/D3D12 false positives are documented in
# `docs/spec/nfr.md:16-19` and `docs/spec/env.md:30` and wired via
# `ASAN_OPTIONS`/`LSAN_OPTIONS` in `tools/env.sh` (leak-gate env:
# GALLIUM_DRIVER=llvmpipe, MESA_GL_VERSION_OVERRIDE=4.6).
option(RE_ENABLE_SANITIZERS "Enable ASan+UBSan on all re_* libs (Debug only, via re_project_sanitizers)" ON)

add_library(re_project_sanitizers INTERFACE)

if(RE_ENABLE_SANITIZERS)
    target_compile_options(re_project_sanitizers INTERFACE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-O1>
    )
    target_link_options(re_project_sanitizers INTERFACE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
endif()
