#include "overlay/KeyboardHook.h"

#include "util/Log.h"
#include "util/Result.h"

namespace threnody::overlay {

KeyboardHook::KeyboardHook(Handler handler) : m_handler(std::move(handler)) {
    if (s_instance != nullptr) {
        log::error("keyboard hook already installed");
        return;
    }
    m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardHook::procedure, GetModuleHandleW(nullptr), 0);
    if (m_hook == nullptr) {
        log::error("{}", Error::fromLastError("SetWindowsHookEx(WH_KEYBOARD_LL)").describe());
        return;
    }
    s_instance = this;
}

KeyboardHook::~KeyboardHook() {
    if (m_hook != nullptr) {
        UnhookWindowsHookEx(m_hook);
        s_instance = nullptr;
    }
}

LRESULT CALLBACK KeyboardHook::procedure(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) && s_instance != nullptr) {
        const auto* event = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (const std::optional<LockKey> key = lockKeyFromVirtualKey(event->vkCode)) {
            // The toggle flips on key down, so by key up GetKeyState reflects
            // the new state. Insert has no toggle worth showing; it is
            // reported as pressed.
            const bool on = *key == LockKey::Insert || (GetKeyState(virtualKeyOf(*key)) & 0x0001) != 0;
            if (s_instance->m_handler) {
                s_instance->m_handler(*key, on);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

}  // namespace threnody::overlay
