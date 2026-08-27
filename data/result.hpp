#pragma once

// data/result.hpp — GL-free typed error handling (SPEC §5, NAMING_CONVENTIONS
// S8).
//
// RenderEngine reports runtime failures through a typed Result<T, Error>
// instead of C++ exceptions (no exceptions in v1). This header is deliberately
// free of any GL / rendering dependency so that every layer (io/, data/,
// volume/ are GL-free; core/ and render/ link against it) can share it.
//
// Error identity is the PAIR (Error::domain, Error::code): numeric code
// ranges intentionally repeat across producers — all three io/ loaders open
// their enums at FileOpen == 1 (ImageLoadError 1..3, MeshLoadError 1..6,
// VolumeLoadError 1..8) — so a consumer that can observe two producer's
// errors branches on the domain first and the code second, never by parsing
// message strings (SPEC §6 "Error codes carry their domain"). Errors built
// through the legacy plain-(code, message) path carry ErrorDomain::None;
// producers stamp their domain via the three-argument makeError overload.

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace re::data {

/// Identifies which enumerated code-space an `Error::code` value belongs to.
///
/// One value per named producer enum (the io/ loader enums collide numerically,
/// hence one entry each), plus one per layer whose APIs report small ad-hoc
/// int codes; `None` marks an error with no owning enumerated range. The
/// enum is complete for every current error-producing layer, so stamping a
/// new call site never requires extending it (OCP).
enum class ErrorDomain : std::int32_t {
    None = 0,     ///< Unset — no owning enumerated range (legacy ad-hoc path).
    ImageIo = 1,  ///< `code` indexes re::io::ImageLoadError (image loader).
    MeshIo = 2,   ///< `code` indexes re::io::MeshLoadError (OBJ mesh loader).
    VolumeIo = 3, ///< `code` indexes re::io::VolumeLoadError (NRRD loader).
    Shader = 4,   ///< `code` indexes re::core::ShaderError (compile/link).
    Core = 5,     ///< core/ window + GL-object ad-hoc codes.
    Utils = 6,    ///< utils/ offscreen-context ad-hoc codes.
    Render = 7,   ///< render/ view-target / renderer-resource ad-hoc codes.
    Broker = 8,   ///< broker/ mapper + asset-store ad-hoc codes.
    Scene = 9,    ///< scene/ stable-handle resolution codes.
};

/// Tag selecting the error branch of a Result.
struct ErrorTag {
    explicit constexpr ErrorTag() = default;
};

/// Tag selecting the value branch of a Result.
struct ValueTag {
    explicit constexpr ValueTag() = default;
};

/// Compile-time tags used to build a Result unambiguously.
inline constexpr ErrorTag error{};
inline constexpr ValueTag value{};

/// An error descriptor carrying a domain tag, an enumerated code interpreted
/// within that domain, and a human-readable message.
struct Error {
    /// Which enumerated range `code` belongs to (never parse `message` to
    /// find out).
    ErrorDomain domain{ErrorDomain::None};
    int code{};
    std::string message{};

    Error() = default;

    /// Legacy untagged form: the error has no owning enumerated range
    /// (`domain == ErrorDomain::None`). Kept so pre-domain call sites keep
    /// compiling unchanged; prefer the domain-tagged constructor.
    Error(int c, std::string m) : code(c), message(std::move(m)) {}

    /// Domain-tagged form: `c` must be a value of the enumeration that
    /// `d` names (e.g. d == ErrorDomain::VolumeIo → c is a VolumeLoadError).
    Error(ErrorDomain d, int c, std::string m)
        : domain(d), code(c), message(std::move(m)) {}
};

/// A value-or-error result. Exactly one of `ok()`/`failed()` is true.
///
/// `T` may be `void` to signal success/failure with no payload. The value
/// branch is stored in a `std::optional`, so `T` need not be
/// default-constructible.
///
/// VG7: [[nodiscard]] on type — ignoring a Result discards a typed error.
/// The Error is embedded (domain+code+message) and retained per T22; monadic
/// helpers `map`/`andThen` are optional but preserved for chaining fallible
/// calls without losing the domain tag.
template <typename T>
class [[nodiscard]] Result {
   public:
    /// Construct an error result.
    Result(ErrorTag, Error e) : ok_(false), err_(std::move(e)) {}

    /// Construct a value result (SFINAE-disabled for void).
    template <typename U = T, typename = std::enable_if_t<!std::is_void_v<U>>>
    Result(ValueTag, U&& v) : ok_(true), val_(std::forward<U>(v)) {}

    /// Move construct from another result.
    Result(Result&&) = default;

    /// Move assign from another result.
    Result& operator=(Result&&) = default;

    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    ~Result() = default;

    /// True if this result holds a value.
    bool ok() const noexcept {
        return ok_;
    }

    /// True if this result holds an error.
    bool failed() const noexcept {
        return !ok_;
    }

    /// Access the value. Branch on ok()/failed() first: dereferencing a
    /// failed result is a contract violation — debug builds assert (abort)
    /// here, release builds leave it undefined behavior. The failure-path
    /// accessor is `error()`, never `operator*`.
    T& operator*() noexcept {
        assert(ok_ && "data::Result::operator* called on a failed Result - "
                      "check ok()/failed() first (failure path is error())");
        return *val_;
    }
    const T& operator*() const noexcept {
        assert(ok_ && "data::Result::operator* called on a failed Result - "
                      "check ok()/failed() first (failure path is error())");
        return *val_;
    }

    /// Access the value via pointer. A failed result asserts in debug builds
    /// (same contract as `operator*`); release builds return nullptr, which
    /// callers must never rely on as control flow — check ok() first.
    T* operator->() noexcept {
        assert(ok_ && "data::Result::operator-> called on a failed Result - "
                      "check ok()/failed() first (failure path is error())");
        return ok_ ? &*val_ : nullptr;
    }
    const T* operator->() const noexcept {
        assert(ok_ && "data::Result::operator-> called on a failed Result - "
                      "check ok()/failed() first (failure path is error())");
        return ok_ ? &*val_ : nullptr;
    }

    /// Access the error (undefined behavior if ok()).
    const Error& error() const noexcept {
        return err_;
    }

    /// Monadic bind (T22 stretch): run `fn` only when this result holds a
    /// value; a failed result short-circuits and propagates its Error
    /// unchanged (same domain/code/message). `fn` maps `T&` to a
    /// `Result<U>`. Available on rvalue results — chain from temporaries or
    /// `std::move` an lvalue. camelCase per NAMING_CONVENTIONS §4 (std::
    /// spells it `and_then`).
    template <typename Fn>
    auto andThen(Fn&& fn) && -> decltype(std::declval<Fn>()(
        std::declval<T&>())) {
        using ResultType =
            decltype(std::declval<Fn>()(std::declval<T&>()));
        if (failed()) {
            return ResultType(ErrorTag{}, std::move(err_));
        }
        return std::forward<Fn>(fn)(*val_);
    }

    /// Monadic map (T22 stretch): apply `fn` to the value when ok(),
    /// producing `Result<U>`; a failed result propagates its Error unchanged
    /// and `fn` is never invoked.
    template <typename Fn>
    auto map(Fn&& fn) && -> Result<std::invoke_result_t<Fn, T&>> {
        using U = std::invoke_result_t<Fn, T&>;
        if (failed()) {
            return Result<U>(ErrorTag{}, std::move(err_));
        }
        return Result<U>(ValueTag{}, std::forward<Fn>(fn)(*val_));
    }

   private:
    bool ok_{false};
    Error err_{};
    std::optional<T> val_;
};

/// Specialization of Result for void payloads (pure success/error signalling).
template <>
class [[nodiscard]] Result<void> {
   public:
    /// Construct an error result.
    Result(ErrorTag, Error e) : ok_(false), err_(std::move(e)) {}

    /// Construct a success result.
    Result(ValueTag) : ok_(true) {}

    /// True if the operation succeeded.
    bool ok() const noexcept {
        return ok_;
    }

    /// True if the operation failed.
    bool failed() const noexcept {
        return !ok_;
    }

    /// Access the error (undefined behavior if ok()).
    const Error& error() const noexcept {
        return err_;
    }

    /// Monadic bind for void payloads (T22 stretch): run the nullary `fn`
    /// only on success; a failed result short-circuits and propagates its
    /// Error unchanged. `fn` returns any `Result<U>` — this is how
    /// sequential fallible calls collapse into one expression.
    template <typename Fn>
    auto andThen(Fn&& fn) && -> decltype(std::forward<Fn>(fn)()) {
        using ResultType = decltype(std::forward<Fn>(fn)());
        if (failed()) {
            return ResultType(ErrorTag{}, std::move(err_));
        }
        return std::forward<Fn>(fn)();
    }

    /// Monadic map for void payloads (T22 stretch): call the nullary `fn`
    /// when ok(), producing `Result<U>`; a failed result propagates its
    /// Error unchanged and `fn` is never invoked.
    template <typename Fn>
    auto map(Fn&& fn) && -> Result<std::invoke_result_t<Fn&>> {
        using U = std::invoke_result_t<Fn&>;
        if (failed()) {
            return Result<U>(ErrorTag{}, std::move(err_));
        }
        return Result<U>(ValueTag{}, std::forward<Fn>(fn)());
    }

   private:
    bool ok_{false};
    Error err_{};
};

/// Convenience factory for an untagged error result (`domain ==
/// ErrorDomain::None`). Prefer the domain-tagged overload below so codes are
/// structurally interpretable.
template <typename T>
[[nodiscard]] Result<T> makeError(int code, std::string message) {
    return Result<T>(error, Error{code, std::move(message)});
}

/// Convenience factory for a domain-tagged error result: `code` is
/// interpreted within `domain` (e.g. `makeError<T>(ErrorDomain::VolumeIo,
/// static_cast<int>(VolumeLoadError::FileOpen), "...")`), keeping colliding
/// numeric ranges across producers distinguishable without string parsing.
template <typename T>
[[nodiscard]] Result<T> makeError(ErrorDomain domain, int code,
                                  std::string message) {
    return Result<T>(error, Error{domain, code, std::move(message)});
}

/// Convenience factory for a value result (void payloads excluded).
template <typename T, typename U>
[[nodiscard]] Result<T> makeValue(U&& v) {
    return Result<T>(value, std::forward<U>(v));
}

} // namespace re::data
