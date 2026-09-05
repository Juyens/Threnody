#pragma once

#include "color/Color.h"
#include "overlay/LockKey.h"
#include "render/Fonts.h"
#include "render/Graphics.h"
#include "render/LayeredSurface.h"
#include "util/Result.h"
#include "util/Win32.h"

#include <Windows.h>

#include <memory>
#include <string>

namespace threnody::overlay {

// The floating "Caps Lock on" style flyout: a small always-on-top, click-
// through layered window centred near the top of the work area. It slides in
// from above while fading in, stays two seconds, and slides back out. The
// padlock's shackle rotates open or closed with a small bounce; the accent
// bar underneath shrinks and dims when the key is off.
class LockKeyOverlay {
public:
    [[nodiscard]] static Result<std::unique_ptr<LockKeyOverlay>> create(HINSTANCE instance);
    ~LockKeyOverlay();

    LockKeyOverlay(const LockKeyOverlay&) = delete;
    LockKeyOverlay& operator=(const LockKeyOverlay&) = delete;

    // Shows the flyout for `key`, or refreshes it if already visible.
    void show(LockKey key, bool on);

private:
    enum class Phase { Hidden, Opening, Shown, Closing };

    struct Animated {
        float from{};
        float to{};
        [[nodiscard]] float at(float eased) const noexcept { return from + (to - from) * eased; }
    };

    LockKeyOverlay(HINSTANCE instance, render::Graphics graphics);
    [[nodiscard]] Result<void> init();

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void frame();
    [[nodiscard]] Result<void> draw(float shackleAngle, float shackleBounce, float indicatorWidth,
                                    float indicatorOpacity);
    [[nodiscard]] Result<void> ensureTarget();
    void place();
    void setPhase(Phase phase);

    HINSTANCE m_instance{};
    render::Graphics m_graphics;
    win32::WindowClass m_class;
    win32::unique_hwnd m_hwnd;
    render::LayeredSurface m_surface;

    winrt::com_ptr<ID2D1DCRenderTarget> m_target;
    winrt::com_ptr<ID2D1SolidColorBrush> m_brush;
    winrt::com_ptr<IDWriteTextFormat> m_textFormat;
    winrt::com_ptr<ID2D1PathGeometry> m_lockBody;
    winrt::com_ptr<ID2D1PathGeometry> m_lockDot;
    winrt::com_ptr<ID2D1PathGeometry> m_lockShackle;
    HDC m_boundDc{};

    UINT m_dpi{96};
    RECT m_workArea{};
    int m_shownTop{};
    int m_hiddenTop{};
    int m_left{};

    std::wstring m_text;
    float m_widthDip{};  // Grows past the default when the text needs it.
    Color m_accent;
    Animated m_shackleAngle;
    Animated m_indicatorWidth;
    Animated m_indicatorOpacity;
    ULONGLONG m_statusStart{};

    Phase m_phase{Phase::Hidden};
    ULONGLONG m_phaseStart{};
    int m_closingFromTop{};
};

}  // namespace threnody::overlay
