#pragma once

#include <Windows.h>

#include <optional>

namespace threnody::overlay {

enum class LockKey { CapsLock, NumLock, ScrollLock, Insert };

[[nodiscard]] constexpr std::optional<LockKey> lockKeyFromVirtualKey(DWORD virtualKey) noexcept {
    switch (virtualKey) {
        case VK_CAPITAL: return LockKey::CapsLock;
        case VK_NUMLOCK: return LockKey::NumLock;
        case VK_SCROLL: return LockKey::ScrollLock;
        case VK_INSERT: return LockKey::Insert;
        default: return std::nullopt;
    }
}

[[nodiscard]] constexpr int virtualKeyOf(LockKey key) noexcept {
    switch (key) {
        case LockKey::CapsLock: return VK_CAPITAL;
        case LockKey::NumLock: return VK_NUMLOCK;
        case LockKey::ScrollLock: return VK_SCROLL;
        case LockKey::Insert: return VK_INSERT;
    }
    return 0;
}

}  // namespace threnody::overlay
