#include "shell/Clipboard.h"

#include "util/Log.h"
#include "util/Result.h"

#include <cstring>

namespace threnody::shell {

bool copyText(HWND owner, std::wstring_view text) {
    if (!OpenClipboard(owner)) {
        log::warn("{}", Error::fromLastError("OpenClipboard").describe());
        return false;
    }
    EmptyClipboard();
    bool copied = false;
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t)); memory) {
        if (auto* chars = static_cast<wchar_t*>(GlobalLock(memory)); chars) {
            std::memcpy(chars, text.data(), text.size() * sizeof(wchar_t));
            chars[text.size()] = L'\0';
            GlobalUnlock(memory);
            copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        }
        if (!copied) {
            GlobalFree(memory);  // The clipboard owns it only once SetClipboardData succeeds.
        }
    }
    CloseClipboard();
    return copied;
}

}  // namespace threnody::shell
