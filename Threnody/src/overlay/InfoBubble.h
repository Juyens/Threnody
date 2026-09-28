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

// A small bubble over part of the widget: an image, an optional label, a
// title and a few lines of detail. Used for what plays next (over "next"),
// the song (over the title) and the artist (over the name). Click-through,
// never activated; fades in when it appears.
class InfoBubble {
public:
    struct Content {
        std::wstring label;               // Small accent line above the title; empty for none.
        std::wstring title;
        std::vector<std::wstring> lines;  // Details under the title.
        std::vector<std::uint8_t> image;  // Encoded image; empty draws a placeholder.
        bool roundImage{false};           // A portrait (the artist) rather than a cover.

        bool operator==(const Content&) const = default;
    };

    [[nodiscard]] static Result<std::unique_ptr<InfoBubble>> create(HINSTANCE instance);
    ~InfoBubble();

    InfoBubble(const InfoBubble&) = delete;
    InfoBubble& operator=(const InfoBubble&) = delete;

    // `anchor` is the part of the widget in screen pixels; the bubble is
    // centred on it, above it (below for a taskbar at the top). Calling it
    // again while shown updates the content in place.
    void show(const RECT& anchor, UINT dpi, const Content& content);
    void hide();
    [[nodiscard]] bool visible() const noexcept;

private:
    InfoBubble(HINSTANCE instance, render::Graphics graphics);
    [[nodiscard]] Result<void> init();

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void frame();
    [[nodiscard]] float textHeightDip() const noexcept;
    [[nodiscard]] float imageSideDip() const noexcept;
    [[nodiscard]] Result<void> draw();
    [[nodiscard]] Result<winrt::com_ptr<ID2D1Bitmap>> decodeImage(UINT sidePx);

    HINSTANCE m_instance{};
    render::Graphics m_graphics;
    win32::WindowClass m_class;
    win32::unique_hwnd m_hwnd;
    render::LayeredSurface m_surface;
    winrt::com_ptr<ID2D1DCRenderTarget> m_target;
    winrt::com_ptr<ID2D1SolidColorBrush> m_brush;
    winrt::com_ptr<IDWriteTextFormat> m_labelFormat;
    winrt::com_ptr<IDWriteTextFormat> m_titleFormat;
    winrt::com_ptr<IDWriteTextFormat> m_lineFormat;

    UINT m_dpi{96};
    Content m_content;
    ULONGLONG m_shownTick{};
    BYTE m_alpha{};
};

}  // namespace threnody::overlay
