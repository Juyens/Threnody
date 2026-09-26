#pragma once

#include "render/Graphics.h"
#include "render/LayeredSurface.h"
#include "util/Result.h"
#include "util/Win32.h"

#include <Windows.h>

#include <array>
#include <functional>
#include <memory>

namespace threnody::overlay {

// Spotify-style volume slider in a small flyout over the widget's volume
// button: a mute button, a track to click or drag, and the percentage. The
// wheel and arrow keys step it; Esc or clicking elsewhere closes it.
//
// The flyout only reports what the user did; the owner applies it and feeds
// the resulting state back with `setState`.
class VolumeFlyout {
public:
    struct Callbacks {
        std::function<void(float level)> onLevel;
        std::function<void()> onToggleMute;
    };

    [[nodiscard]] static Result<std::unique_ptr<VolumeFlyout>> create(HINSTANCE instance, Callbacks callbacks);
    ~VolumeFlyout();

    VolumeFlyout(const VolumeFlyout&) = delete;
    VolumeFlyout& operator=(const VolumeFlyout&) = delete;

    // `anchor` is the button in screen pixels. The flyout is centred on it,
    // above it (or below, for a taskbar at the top of the screen).
    void open(const RECT& anchor, UINT dpi, float level, bool muted);
    void close();
    void setState(float level, bool muted);

    [[nodiscard]] bool visible() const noexcept;
    // True for a moment after the flyout closed because the user clicked
    // elsewhere. When that click was on the volume button itself, it must
    // not reopen what it just closed.
    [[nodiscard]] bool justClosed() const noexcept;

private:
    VolumeFlyout(HINSTANCE instance, render::Graphics graphics, Callbacks callbacks);
    [[nodiscard]] Result<void> init();

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void frame();
    void redraw();
    [[nodiscard]] Result<void> draw();
    [[nodiscard]] Result<void> ensureTarget();
    [[nodiscard]] float toDip(int px) const noexcept;
    [[nodiscard]] float trackLeft() const noexcept;
    [[nodiscard]] float trackRight() const noexcept;
    void setFromPointer(int xPx);
    void step(float delta);

    HINSTANCE m_instance{};
    render::Graphics m_graphics;
    Callbacks m_callbacks;
    win32::WindowClass m_class;
    win32::unique_hwnd m_hwnd;
    render::LayeredSurface m_surface;

    winrt::com_ptr<ID2D1DCRenderTarget> m_target;
    winrt::com_ptr<ID2D1SolidColorBrush> m_brush;
    winrt::com_ptr<IDWriteTextFormat> m_textFormat;
    std::array<winrt::com_ptr<ID2D1PathGeometry>, 4> m_speakerIcons;  // Muted, low, mid, high.
    HDC m_boundDc{};

    UINT m_dpi{96};
    float m_level{};
    bool m_muted{false};
    bool m_dragging{false};
    bool m_hovering{false};
    ULONGLONG m_openedTick{};
    ULONGLONG m_closedTick{};
    BYTE m_alpha{};
};

}  // namespace threnody::overlay
