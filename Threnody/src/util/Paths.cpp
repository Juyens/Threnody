#include "util/Paths.h"

#include "Config.h"
#include "util/Win32.h"

#include <ShlObj.h>

namespace threnody::paths {

Result<std::filesystem::path> dataDirectory() {
    wchar_t* raw = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw);
    win32::unique_cotaskmem<wchar_t> folder{raw};
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "SHGetKnownFolderPath(LocalAppData)");
    }

    std::filesystem::path directory{folder.get()};
    directory /= config::appDataFolderName;

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return Error::fromWin32(static_cast<DWORD>(ec.value()), "create data directory");
    }
    return directory;
}

}  // namespace threnody::paths
