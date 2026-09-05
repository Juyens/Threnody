#pragma once

#include <Windows.h>
#include <combaseapi.h>

#include <memory>
#include <type_traits>

// RAII wrappers for raw Win32 handles. Nothing here should ever need a manual
// Close/Destroy call at a use site.
namespace threnody::win32 {

struct HwndDeleter {
    void operator()(HWND hwnd) const noexcept { DestroyWindow(hwnd); }
};
using unique_hwnd = std::unique_ptr<std::remove_pointer_t<HWND>, HwndDeleter>;

struct HandleDeleter {
    void operator()(HANDLE handle) const noexcept {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};
using unique_handle = std::unique_ptr<void, HandleDeleter>;

struct HkeyDeleter {
    void operator()(HKEY key) const noexcept { RegCloseKey(key); }
};
using unique_hkey = std::unique_ptr<std::remove_pointer_t<HKEY>, HkeyDeleter>;

struct HdcDeleter {
    void operator()(HDC dc) const noexcept { DeleteDC(dc); }
};
using unique_hdc = std::unique_ptr<std::remove_pointer_t<HDC>, HdcDeleter>;

struct GdiObjectDeleter {
    void operator()(HGDIOBJ object) const noexcept { DeleteObject(object); }
};
using unique_hbitmap = std::unique_ptr<std::remove_pointer_t<HBITMAP>, GdiObjectDeleter>;

struct CoTaskMemDeleter {
    void operator()(void* memory) const noexcept { CoTaskMemFree(memory); }
};
template <class T>
using unique_cotaskmem = std::unique_ptr<T, CoTaskMemDeleter>;

// Registers a window class on construction and unregisters it on destruction.
class WindowClass {
public:
    explicit WindowClass(const WNDCLASSEXW& description);
    ~WindowClass();

    WindowClass(const WindowClass&) = delete;
    WindowClass& operator=(const WindowClass&) = delete;

    [[nodiscard]] bool registered() const noexcept { return m_atom != 0; }
    [[nodiscard]] const wchar_t* name() const noexcept { return m_name; }

private:
    HINSTANCE m_instance{};
    const wchar_t* m_name{};
    ATOM m_atom{};
};

[[nodiscard]] constexpr int scaleDip(int dip, UINT dpi) noexcept {
    return (dip * static_cast<int>(dpi) + 48) / 96;
}

[[nodiscard]] constexpr int width(const RECT& rect) noexcept { return rect.right - rect.left; }
[[nodiscard]] constexpr int height(const RECT& rect) noexcept { return rect.bottom - rect.top; }

[[nodiscard]] constexpr bool sameRect(const RECT& a, const RECT& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

}  // namespace threnody::win32
