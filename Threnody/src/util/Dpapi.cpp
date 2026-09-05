#include "util/Dpapi.h"

#include <Windows.h>
#include <dpapi.h>
#include <wincrypt.h>

#include <memory>
#include <vector>

namespace threnody::dpapi {
namespace {

struct LocalFreeDeleter {
    void operator()(void* memory) const noexcept { LocalFree(memory); }
};

Result<std::string> toBase64(const BYTE* data, DWORD size) {
    DWORD length = 0;
    if (!CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &length)) {
        return Error::fromLastError("CryptBinaryToString(size)");
    }
    std::string text(length, '\0');
    if (!CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, text.data(), &length)) {
        return Error::fromLastError("CryptBinaryToString");
    }
    text.resize(length);  // Drops the terminator counted in `length`.
    while (!text.empty() && text.back() == '\0') {
        text.pop_back();
    }
    return text;
}

Result<std::vector<BYTE>> fromBase64(std::string_view text) {
    DWORD size = 0;
    if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, nullptr, &size, nullptr,
                              nullptr)) {
        return Error::fromLastError("CryptStringToBinary(size)");
    }
    std::vector<BYTE> bytes(size);
    if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, bytes.data(), &size,
                              nullptr, nullptr)) {
        return Error::fromLastError("CryptStringToBinary");
    }
    bytes.resize(size);
    return bytes;
}

}  // namespace

Result<std::string> protect(std::string_view secret) {
    DATA_BLOB input{.cbData = static_cast<DWORD>(secret.size()),
                    .pbData = reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Threnody", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return Error::fromLastError("CryptProtectData");
    }
    std::unique_ptr<void, LocalFreeDeleter> owned{output.pbData};
    return toBase64(output.pbData, output.cbData);
}

Result<std::string> unprotect(std::string_view protectedBase64) {
    Result<std::vector<BYTE>> bytes = fromBase64(protectedBase64);
    if (!bytes) {
        return bytes.error();
    }
    DATA_BLOB input{.cbData = static_cast<DWORD>(bytes->size()), .pbData = bytes->data()};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return Error::fromLastError("CryptUnprotectData");
    }
    std::unique_ptr<void, LocalFreeDeleter> owned{output.pbData};
    return std::string{reinterpret_cast<const char*>(output.pbData), output.cbData};
}

}  // namespace threnody::dpapi
