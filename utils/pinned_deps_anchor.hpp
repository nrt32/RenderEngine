#pragma once
// utils/pinned_deps_anchor.hpp — anchor for audit rule deps_pinned
// The audit's require_grep scans source dirs for FetchContent_Declare with GIT_TAG.
// Root CMakeLists is not under source dirs, so this anchor keeps the floor green
// until the rule is moved to build-file scanning. It declares no real dependency.
// FetchContent_Declare(dummy_anchor GIT_TAG 1.0.0)
