#pragma once

// data/result.hpp — GL-free typed error handling (SPEC S5, NAMING_CONVENTIONS
// S8).
//
// RenderEngine reports runtime failures through a typed Result<T, Error>
// instead of C++ exceptions (no exceptions in v1). This header is deliberately
// free of any GL / rendering dependency so that every layer (io/, data/,
// volume/ are GL-free; core/ and render/ link against it) can share it.

#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace re::data {

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

/// An error descriptor carrying an enumerated code and a human-readable
/// message.
struct Error {
    int code{};
    std::string message{};

    Error() = default;
    Error(int c, std::string m) : code(c), message(std::move(m)) {}
};

/// A value-or-error result. Exactly one of `ok()`/`failed()` is true.
///
/// `T` may be `void` to signal success/failure with no payload. The value
/// branch is stored in a `std::optional`, so `T` need not be
/// default-constructible.
template <typename T>
class Result {
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

    /// True if this result carries a value payload.
    bool hasValue() const noexcept {
        return ok_ && val_.has_value();
    }

    /// Access the value (undefined behavior if !ok()).
    T& operator*() noexcept {
        return *val_;
    }
    const T& operator*() const noexcept {
        return *val_;
    }

    /// Access the value via pointer (nullptr if !ok()).
    T* operator->() noexcept {
        return ok_ ? &*val_ : nullptr;
    }
    const T* operator->() const noexcept {
        return ok_ ? &*val_ : nullptr;
    }

    /// Access the error (undefined behavior if ok()).
    const Error& error() const noexcept {
        return err_;
    }

   private:
    bool ok_{false};
    Error err_{};
    std::optional<T> val_;
};

/// Specialization of Result for void payloads (pure success/error signalling).
template <>
class Result<void> {
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

   private:
    bool ok_{false};
    Error err_{};
};

/// Convenience factory for an error result.
template <typename T>
[[nodiscard]] Result<T> makeError(int code, std::string message) {
    return Result<T>(error, Error{code, std::move(message)});
}

/// Convenience factory for a value result (void payloads excluded).
template <typename T, typename U>
[[nodiscard]] Result<T> makeValue(U&& v) {
    return Result<T>(value, std::forward<U>(v));
}

} // namespace re::data
