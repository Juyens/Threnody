#pragma once

#include <Windows.h>

#include <functional>

namespace threnody::interaction {

// Low-level mouse hook that catches the wheel while the pointer is over the
// widget. The widget never has focus (it lives in the taskbar, or floats
// without activating), so ordinary WM_MOUSEWHEEL delivery cannot be relied
// on. Installed only while the pointer is over the widget; the handler says
// whether it took the event, in which case nothing else sees it.
class WheelHook {
public:
    // `delta` in WHEEL_DELTA units, positive away from the user.
    using Handler = std::function<bool(POINT screen, int delta)>;

    explicit WheelHook(Handler handler);
    ~WheelHook();

    WheelHook(const WheelHook&) = delete;
    WheelHook& operator=(const WheelHook&) = delete;

private:
    static LRESULT CALLBACK procedure(int code, WPARAM wParam, LPARAM lParam);

    static inline WheelHook* s_instance{};
    HHOOK m_hook{};
    Handler m_handler;
};

}  // namespace threnody::interaction
