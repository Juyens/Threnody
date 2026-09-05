#pragma once

#include "overlay/LockKey.h"

#include <Windows.h>

#include <functional>

namespace threnody::overlay {

// Low-level keyboard hook that reports lock-key releases with the resulting
// toggle state. The callback runs on the installing thread's message loop,
// so it never blocks typing in other apps as long as the handler is quick;
// here it only forwards. One instance per process.
class KeyboardHook {
public:
    using Handler = std::function<void(LockKey key, bool on)>;

    explicit KeyboardHook(Handler handler);
    ~KeyboardHook();

    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;

    [[nodiscard]] bool installed() const noexcept { return m_hook != nullptr; }

private:
    static LRESULT CALLBACK procedure(int code, WPARAM wParam, LPARAM lParam);

    static inline KeyboardHook* s_instance{};
    HHOOK m_hook{};
    Handler m_handler;
};

}  // namespace threnody::overlay
