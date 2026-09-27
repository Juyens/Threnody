#pragma once

#include "render/Graphics.h"
#include "render/LayeredSurface.h"
#include "util/Result.h"
#include "util/Win32.h"

#include <Windows.h>

#include <memory>

namespace threnody::taskbar {

// The ghost of the widget's slot in the taskbar, shown while a floating
// widget is dragged close enough to dock: a translucent rounded panel with a
// bright outline, fading in. Click-through and never activated.
class DockPreview {
public:
    [[nodiscard]] static Result<std::unique_ptr<DockPreview>> create(HINSTANCE instance);
    ~DockPreview();

    DockPreview(const DockPreview&) = delete;
    DockPreview& operator=(const DockPreview&) = delete;

    // `rect` in screen pixels.
    void show(const RECT& rect, UINT dpi);
    void hide();

private:
    DockPreview(HINSTANCE instance, render::Graphics graphics);
    [[nodiscard]] Result<void> init();

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void frame();
    [[nodiscard]] Result<void> draw();

    HINSTANCE m_instance{};
    render::Graphics m_graphics;
    win32::WindowClass m_class;
    win32::unique_hwnd m_hwnd;
    render::LayeredSurface m_surface;
    winrt::com_ptr<ID2D1DCRenderTarget> m_target;
    winrt::com_ptr<ID2D1SolidColorBrush> m_brush;

    UINT m_dpi{96};
    RECT m_rect{};
    ULONGLONG m_shownTick{};
};

}  // namespace threnody::taskbar
