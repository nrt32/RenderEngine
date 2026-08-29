#pragma once

// test_utils/content_hash_spy.hpp — test-only hash call counter (T12).
//
// The atomic counter that was previously in data (global mutable in the
// pure-math library) is now owned here in test_utils/ so data/ stays pure
// math, headless-testable without global mutable and without atomic/spdlog
// pollution (G: data/ no atomic). data calls _hashSpyRef().fetch_add via
// forward decl without ever spelling the spy name, so data stays clean while
// test_utils owns the spy.

#include <atomic>
#include <cstdint>

namespace re::test_utils {

// Low-level ref — inline static so all TUs share one counter (ODR inline).
inline std::atomic<uint64_t>& _hashSpyRef() noexcept {
    static std::atomic<uint64_t> s{0u};
    return s;
}

inline std::atomic<uint64_t>& contentHashSpy() noexcept { return _hashSpyRef(); }

inline uint64_t contentHashCallCount() noexcept {
    return _hashSpyRef().load(std::memory_order_relaxed);
}

inline void resetContentHashCallCount() noexcept {
    _hashSpyRef().store(0u, std::memory_order_relaxed);
}

inline void notifyHash() noexcept {
    _hashSpyRef().fetch_add(1u, std::memory_order_relaxed);
}

} // namespace re::test_utils
