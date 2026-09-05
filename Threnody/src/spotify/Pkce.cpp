#include "spotify/Pkce.h"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <memory>
#include <vector>

namespace threnody::spotify::pkce {
namespace {

constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

struct AlgorithmDeleter {
    void operator()(BCRYPT_ALG_HANDLE handle) const noexcept { BCryptCloseAlgorithmProvider(handle, 0); }
};
struct HashDeleter {
    void operator()(BCRYPT_HASH_HANDLE handle) const noexcept { BCryptDestroyHash(handle); }
};

constexpr HRESULT fromNtStatus(NTSTATUS status) noexcept {
    return HRESULT_FROM_NT(status);
}

}  // namespace

std::string base64Url(const unsigned char* data, std::size_t size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const unsigned int a = data[i];
        const unsigned int b = i + 1 < size ? data[i + 1] : 0;
        const unsigned int c = i + 2 < size ? data[i + 2] : 0;
        const unsigned int triple = (a << 16) | (b << 8) | c;
        out.push_back(alphabet[(triple >> 18) & 0x3F]);
        out.push_back(alphabet[(triple >> 12) & 0x3F]);
        if (i + 1 < size) {
            out.push_back(alphabet[(triple >> 6) & 0x3F]);
        }
        if (i + 2 < size) {
            out.push_back(alphabet[triple & 0x3F]);
        }
    }
    return out;
}

Result<std::string> randomToken(std::size_t bytes) {
    std::vector<unsigned char> buffer(bytes);
    const NTSTATUS status =
        BCryptGenRandom(nullptr, buffer.data(), static_cast<ULONG>(buffer.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        return Error::fromHResult(fromNtStatus(status), "BCryptGenRandom");
    }
    return base64Url(buffer.data(), buffer.size());
}

Result<std::string> challenge(std::string_view verifier) {
    BCRYPT_ALG_HANDLE rawAlgorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&rawAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        return Error::fromHResult(fromNtStatus(status), "BCryptOpenAlgorithmProvider(SHA256)");
    }
    std::unique_ptr<void, AlgorithmDeleter> algorithm{rawAlgorithm};

    BCRYPT_HASH_HANDLE rawHash = nullptr;
    status = BCryptCreateHash(algorithm.get(), &rawHash, nullptr, 0, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        return Error::fromHResult(fromNtStatus(status), "BCryptCreateHash");
    }
    std::unique_ptr<void, HashDeleter> hash{rawHash};

    status = BCryptHashData(hash.get(), reinterpret_cast<PUCHAR>(const_cast<char*>(verifier.data())),
                            static_cast<ULONG>(verifier.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        return Error::fromHResult(fromNtStatus(status), "BCryptHashData");
    }
    std::array<unsigned char, 32> digest{};
    status = BCryptFinishHash(hash.get(), digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        return Error::fromHResult(fromNtStatus(status), "BCryptFinishHash");
    }
    return base64Url(digest.data(), digest.size());
}

}  // namespace threnody::spotify::pkce
