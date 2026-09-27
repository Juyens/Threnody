#pragma once

#include "render/Graphics.h"
#include "render/LayeredSurface.h"
#include "util/Result.h"
#include "util/Win32.h"

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace threnody::overlay {

// A small bubble over the widget's next button showing what plays next: the
// cover, a label, the title and the artist. Click-through, never activated;
// fades in when it appears.
class QueuePeek {
public:
    struct Content {
        std::wstring label;
        std::wstring title;
        std::wstring subtitle;
        std::vector<std::uint8_t> cover;  // Encoded image; empty draws a placeholder.

        bool operator==(const Content&) const = default;
    };

    [[nodiscard]] static Result<std::unique_ptr<QueuePeek>> create(HINSTANCE instance);
    ~QueuePeek();

    QueuePeek(const QueuePeek&) = delete;
    QueuePeek& operator=(const QueuePeek&) = delete;

    // `anchor` is the button in screen pixels; the bubble is centred on it,
    // above it (below for a taskbar at the top). Calling it again while shown
    // updates the content in place.
    void show(const RECT& anchor, UINT dpi, const Content& content);
    void hide();
    [[nodiscard]] bool visible() const noexcept;

private:
    QueuePeek(HINSTANCE instance, render::Graphics graphics);
    [[nodiscard]] Result<void> init();

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void frame();
    [[nodiscard]] Result<void> draw();
    [[nodiscard]] Result<winrt::com_ptr<ID2D1Bitmap>> decodeCover();

    HINSTANCE m_instance{};
    render::Graphics m_graphics;
    win32::WindowClass m_class;
    win32::unique_hwnd m_hwnd;
    render::LayeredSurface m_surface;
    winrt::com_ptr<ID2D1DCRenderTarget> m_target;
    winrt::com_ptr<ID2D1SolidColorBrush> m_brush;
    winrt::com_ptr<IDWriteTextFormat> m_labelFormat;
    winrt::com_ptr<IDWriteTextFormat> m_titleFormat;
    winrt::com_ptr<IDWriteTextFormat> m_subtitleFormat;

    UINT m_dpi{96};
    Content m_content;
    ULONGLONG m_shownTick{};
    BYTE m_alpha{};
};

}  // namespace threnody::overlay
