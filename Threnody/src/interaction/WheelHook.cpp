#include "interaction/WheelHook.h"

#include "util/Log.h"
#include "util/Result.h"

namespace threnody::interaction {

WheelHook::WheelHook(Handler handler) : m_handler(std::move(handler)) {
    if (s_instance != nullptr) {
        log::error("wheel hook already installed");
        return;
    }
    m_hook = SetWindowsHookExW(WH_MOUSE_LL, &WheelHook::procedure, GetModuleHandleW(nullptr), 0);
    if (m_hook == nullptr) {
        log::error("{}", Error::fromLastError("SetWindowsHookEx(WH_MOUSE_LL)").describe());
        return;
    }
    s_instance = this;
}

WheelHook::~WheelHook() {
    if (m_hook != nullptr) {
        UnhookWindowsHookEx(m_hook);
        s_instance = nullptr;
    }
}

LRESULT CALLBACK WheelHook::procedure(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == WM_MOUSEWHEEL && s_instance != nullptr && s_instance->m_handler) {
        const auto* event = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        const int delta = static_cast<short>(HIWORD(event->mouseData));
        if (s_instance->m_handler(event->pt, delta)) {
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

}  // namespace threnody::interaction
