#include "util/Result.h"

#include "util/Text.h"

#include <format>

namespace threnody {

Error Error::fromLastError(std::string context) noexcept {
    return fromWin32(GetLastError(), std::move(context));
}

Error Error::fromWin32(DWORD code, std::string context) noexcept {
    return Error{.hr = HRESULT_FROM_WIN32(code), .context = std::move(context)};
}

Error Error::fromHResult(HRESULT hr, std::string context) noexcept {
    return Error{.hr = hr, .context = std::move(context)};
}

std::string Error::describe() const {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);

    std::string message;
    if (length != 0 && buffer != nullptr) {
        std::wstring_view text{buffer, length};
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
            text.remove_suffix(1);
        }
        message = text::toUtf8(text);
        LocalFree(buffer);
    } else {
        message = "unknown error";
    }

    return std::format("{}: {} (0x{:08X})", context, message, static_cast<unsigned long>(hr));
}

}  // namespace threnody
