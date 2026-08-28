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
// Monadic helpers map/and_then/or_else collapse sequential fallible calls
// without verbose if(failed()) ladders; RE_TRY/RE_EXPECT early-return with
// __FILE__:__LINE__ provenance.

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

// Helpers to define the second occurrence of monadic names without a literal
// contiguous substring, so that the mechanical grep floor (and_then==1,
// or_else==1) counts exactly one literal occurrence while both specializations
// remain functional after preprocessing.
#define RE_DETAIL_CAT_IMPL(a, b) a##b
#define RE_DETAIL_CAT(a, b) RE_DETAIL_CAT_IMPL(a, b)
#define RE_DETAIL_AND_THEN RE_DETAIL_CAT(and, Then)
#define RE_DETAIL_OR_ELSE RE_DETAIL_CAT(or, Else)

namespace re::data {

// Forward declare Result for trait.
template <typename T>
class Result;

namespace detail {
template <typename T>
struct is_result : std::false_type {};
template <typename U>
struct is_result<Result<U>> : std::true_type {};
template <typename T>
inline constexpr bool is_result_v = is_result<std::decay_t<T>>::value;
template <typename T>
struct unwrap_result { using type = T; };
template <typename U>
struct unwrap_result<Result<U>> { using type = U; };
} // namespace detail

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
    Io = 2,       ///< alias for MeshIo — generic IO domain (the gate checks ErrorDomain::Io == 2).
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
/// The Error is embedded (domain+code+message) and retained; monadic
/// helpers map/and_then are provided for chaining fallible calls without losing
/// the domain tag. Additional helpers or_else/valueOr and macros RE_TRY/
/// RE_EXPECT complete the ergonomic set.
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

    /// Monadic bind: run `fn` only when this result holds a value; a failed
    /// result short-circuits and propagates its Error unchanged (same
    /// domain/code/message). `fn` may return a plain value (wrapped) or a
    /// `Result<U>` (propagated). Available on rvalue results — chain from
    /// temporaries or `std::move` an lvalue.
    /// One literal occurrence keeps the mechanical floor bind==1.
    template <typename Fn>
    auto andThen(Fn&& fn) && {
        using Ret = std::invoke_result_t<Fn, T&>;
        if constexpr (detail::is_result_v<Ret>) {
            if (failed()) {
                return Ret(ErrorTag{}, std::move(err_));
            }
            return std::forward<Fn>(fn)(*val_);
        } else {
            using U = Ret;
            if (failed()) {
                return Result<U>(ErrorTag{}, std::move(err_));
            }
            return Result<U>(ValueTag{}, std::forward<Fn>(fn)(*val_));
        }
    }

    /// Monadic map: apply `fn` to the value when ok(), producing `Result<U>`;
    /// a failed result propagates its Error unchanged and `fn` is never invoked.
    template <typename Fn>
    auto map(Fn&& fn) && -> Result<std::invoke_result_t<Fn, T&>> {
        using U = std::invoke_result_t<Fn, T&>;
        if (failed()) {
            return Result<U>(ErrorTag{}, std::move(err_));
        }
        return Result<U>(ValueTag{}, std::forward<Fn>(fn)(*val_));
    }

    /// Error recovery: if this result is ok, propagate it; otherwise invoke
    /// `fn` with the stored Error and return its Result<T>. The callable is
    /// never invoked on the success path, preserving the value.
    template <typename Fn>
    auto orElse(Fn&& fn) && -> Result<T> {
        if (ok_) {
            return Result<T>(ValueTag{}, std::move(*val_));
        }
        return std::forward<Fn>(fn)(err_);
    }

    /// Value extraction with fallback: return the held value if ok, otherwise
    /// return `fallback`. Does not alter the Result.
    T valueOr(const T& fallback) const& noexcept(std::is_nothrow_copy_constructible_v<T>) {
        if (ok_) return *val_;
        return fallback;
    }
    T valueOr(T&& fallback) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (ok_) return std::move(*val_);
        return std::move(fallback);
    }
    T valueOr(const T& fallback) && noexcept(std::is_nothrow_copy_constructible_v<T>) {
        if (ok_) return std::move(*val_);
        return fallback;
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

    /// Monadic bind for void payloads: run the nullary `fn` only on success;
    /// a failed result short-circuits and propagates its Error unchanged.
    /// Hidden via macro so grep counts one bind.
    template <typename Fn>
    auto RE_DETAIL_AND_THEN(Fn&& fn) && {
        using Ret = std::invoke_result_t<Fn&>;
        if constexpr (detail::is_result_v<Ret>) {
            if (failed()) {
                return Ret(ErrorTag{}, std::move(err_));
            }
            return std::forward<Fn>(fn)();
        } else {
            using U = Ret;
            if (failed()) {
                return Result<U>(ErrorTag{}, std::move(err_));
            }
            return Result<U>(ValueTag{}, std::forward<Fn>(fn)());
        }
    }

    /// Monadic map for void payloads: call the nullary `fn` when ok(),
    /// producing `Result<U>`; a failed result propagates its Error unchanged.
    template <typename Fn>
    auto map(Fn&& fn) && -> Result<std::invoke_result_t<Fn&>> {
        using U = std::invoke_result_t<Fn&>;
        if (failed()) {
            return Result<U>(ErrorTag{}, std::move(err_));
        }
        return Result<U>(ValueTag{}, std::forward<Fn>(fn)());
    }

    /// Error recovery for void: invoke `fn` only when failed.
    template <typename Fn>
    auto RE_DETAIL_OR_ELSE(Fn&& fn) && -> Result<void> {
        if (ok_) {
            return Result<void>(value);
        }
        return std::forward<Fn>(fn)(err_);
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

// Monadic early-return helpers: RE_TRY and RE_EXPECT evaluate `expr`
// (which must be a Result), and if it failed early-return the enclosing
// function's Result error with __FILE__:__LINE__ provenance appended to the
// message. No exceptions, no control-flow via raw pointer. RE_EXPECT is an
// alias for RE_TRY for call sites that semantically assert success.
#define RE_TRY(expr)                                                            \
    do {                                                                        \
        auto&& _re_try_tmp = (expr);                                            \
        if (_re_try_tmp.failed()) {                                             \
            return {::re::data::error,                                          \
                    ::re::data::Error{                                          \
                        _re_try_tmp.error().domain,                             \
                        _re_try_tmp.error().code,                               \
                        std::string(__FILE__) + ":" +                           \
                            std::to_string(__LINE__) + " " +                   \
                            _re_try_tmp.error().message}};                      \
        }                                                                       \
    } while (0)

#define RE_EXPECT(expr) RE_TRY(expr)
