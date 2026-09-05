#pragma once

#include <Windows.h>

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace threnody {

// Failure description carried by Result. `context` says what was being
// attempted so a log line can be understood without reproducing the fault.
struct Error {
    HRESULT hr{E_FAIL};
    std::string context;

    [[nodiscard]] static Error fromLastError(std::string context) noexcept;
    [[nodiscard]] static Error fromHResult(HRESULT hr, std::string context) noexcept;
    [[nodiscard]] static Error fromWin32(DWORD code, std::string context) noexcept;

    // "context: system message (0x8000FFFF)"
    [[nodiscard]] std::string describe() const;
};

// Minimal stand-in for std::expected<T, Error>, which MSVC only ships in C++23
// mode. Holds either a value or an Error; `ok()` tells which.
template <class T>
class [[nodiscard]] Result {
public:
    Result(T value) : m_state(std::move(value)) {}
    Result(Error error) : m_state(std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(m_state); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & { return std::get<T>(m_state); }
    [[nodiscard]] const T& value() const& { return std::get<T>(m_state); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(m_state)); }
    [[nodiscard]] const Error& error() const { return std::get<Error>(m_state); }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }

private:
    std::variant<T, Error> m_state;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : m_error(std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return !m_error.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
    [[nodiscard]] const Error& error() const { return *m_error; }

private:
    std::optional<Error> m_error;
};

}  // namespace threnody
