#pragma once

#include "color/Color.h"

#include <cstddef>

// Every user-facing preference that is not exposed in the settings menu lives
// here as a compile-time constant. Lengths are device-independent pixels
// (96 DPI) unless the name says otherwise.
namespace threnody::config {

// Widget placement inside the taskbar.
inline constexpr int widgetMaxWidthDip = 480;
inline constexpr int widgetVerticalMarginDip = 4;    // Gap above and below, inside the taskbar.
inline constexpr int widgetEdgeMarginDip = 12;       // Gap to the screen edge or to the tray.

// Widget interior, left to right: cover, text, controls, visualiser.
inline constexpr float widgetPaddingDip = 6.0f;
inline constexpr float widgetGapDip = 10.0f;
inline constexpr float backgroundCornerRadiusDip = 6.0f;
inline constexpr float coverCornerRadiusDip = 4.0f;
inline constexpr float textMaxWidthDip = 170.0f;
inline constexpr float textLineGapDip = 1.0f;
inline constexpr float controlButtonWidthDip = 22.0f;
inline constexpr float controlGlyphSizeDip = 10.0f;

// Spectrum visualiser: geometry.
inline constexpr int spectrumBarCount = 13;
inline constexpr float spectrumBarWidthDip = 3.0f;
inline constexpr float spectrumBarGapDip = 2.0f;
inline constexpr float spectrumBaselineDip = 2.0f;

// Spectrum visualiser: analysis. Bands are log-spaced between the two
// frequencies; levels are mapped linearly between the dB floor and ceiling.
// Attack/release are per-frame blend factors at the visualiser frame rate.
inline constexpr double spectrumMinHz = 40.0;
inline constexpr double spectrumMaxHz = 8000.0;
inline constexpr float spectrumFloorDb = -62.0f;
inline constexpr float spectrumCeilingDb = -14.0f;
// Music rolls off toward the treble; lift each band by this much per octave
// above the lowest so the right-hand bars get to move too.
inline constexpr float spectrumTiltDbPerOctave = 3.5f;
inline constexpr float spectrumAttack = 0.65f;
inline constexpr float spectrumRelease = 0.86f;
inline constexpr unsigned spectrumFrameMs = 33;  // ~30 fps

// Rainbow colour mode: how much of the hue circle the thirteen bars span at
// once, how fast the gradient travels (full cycle in this many seconds), and
// the saturation/value of the bars.
inline constexpr float rainbowHueSpan = 0.75f;
inline constexpr float rainbowCycleSeconds = 6.0f;
inline constexpr float rainbowSaturation = 0.80f;
inline constexpr float rainbowValue = 1.0f;

// Cover colour analysis works on a downscaled copy of this many pixels a side.
inline constexpr unsigned coverSampleSize = 48;

// How long to wait before retrying a failed or lost audio capture.
inline constexpr unsigned captureRetryMs = 10000;

// Text. DirectWrite handles shaping; the fallback chain covers CJK titles.
inline constexpr wchar_t fontFamily[] = L"Segoe UI Variable Text";
inline constexpr wchar_t fontFamilyJapanese[] = L"Yu Gothic UI";
inline constexpr wchar_t fontFamilyChinese[] = L"Microsoft YaHei UI";
inline constexpr wchar_t fontFamilyKorean[] = L"Malgun Gothic";
inline constexpr float titleFontSizeDip = 12.5f;
inline constexpr float artistFontSizeDip = 11.0f;

// Shown while Spotify has no media session.
inline constexpr wchar_t placeholderTitle[] = L"Spotify";
inline constexpr wchar_t placeholderArtist[] = L"Nada en reproducción";

// Colours, straight alpha. Tuned for the dark Windows 11 taskbar.
inline constexpr Color backgroundColor{1.0f, 1.0f, 1.0f, 0.07f};
inline constexpr Color backgroundBorderColor{1.0f, 1.0f, 1.0f, 0.06f};
inline constexpr Color coverPlaceholderColor{1.0f, 1.0f, 1.0f, 0.12f};
inline constexpr Color titleColor{1.0f, 1.0f, 1.0f, 0.95f};
inline constexpr Color artistColor{1.0f, 1.0f, 1.0f, 0.60f};
inline constexpr Color controlColor{1.0f, 1.0f, 1.0f, 0.90f};
inline constexpr Color defaultAccentColor{0.55f, 0.78f, 1.0f, 1.0f};

// Lock-key overlay. Sizes and timings follow the reference flyout: a
// 160 x 50 panel, 300 ms slide with a 2000 ms hold, 200 ms status animation.
inline constexpr float lockOverlayWidthDip = 160.0f;
inline constexpr float lockOverlayHeightDip = 50.0f;
inline constexpr int lockOverlayTopMarginDip = 16;      // Distance from the top of the work area when shown.
inline constexpr float lockOverlayCornerRadiusDip = 8.0f;
inline constexpr float lockOverlayPaddingLeftDip = 14.0f;
inline constexpr float lockOverlayPaddingRightDip = 12.0f;
inline constexpr float lockOverlayPaddingBottomDip = 6.0f;
inline constexpr float lockOverlayIconSizeDip = 22.0f;
inline constexpr float lockOverlayTextLeftMarginDip = 20.0f;
inline constexpr float lockOverlayTextBottomMarginDip = 4.0f;
inline constexpr float lockOverlayTextSlackDip = 10.0f;  // Extra room when the panel grows to fit the text.
inline constexpr float lockOverlayFontSizeDip = 14.0f;
inline constexpr float lockOverlayIndicatorWidthDip = 60.0f;
inline constexpr float lockOverlayIndicatorOffWidthDip = 36.0f;
inline constexpr float lockOverlayIndicatorHeightDip = 4.0f;
inline constexpr float lockOverlayIndicatorOffOpacity = 0.2f;
inline constexpr float lockOverlayShackleOpenDegrees = 25.0f;
inline constexpr unsigned lockOverlaySlideMs = 300;
inline constexpr unsigned lockOverlayHoldMs = 2000;
inline constexpr unsigned lockOverlayStatusMs = 200;
inline constexpr Color lockOverlayBackgroundColor{0.125f, 0.125f, 0.125f, 0.90f};
inline constexpr Color lockOverlayBorderColor{1.0f, 1.0f, 1.0f, 0.09f};
inline constexpr Color lockOverlayForegroundColor{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr wchar_t lockOverlayCapsLockName[] = L"Bloq Mayús";
inline constexpr wchar_t lockOverlayNumLockName[] = L"Bloq Num";
inline constexpr wchar_t lockOverlayScrollLockName[] = L"Bloq Despl";
inline constexpr wchar_t lockOverlayInsertText[] = L"Insert pulsado";
inline constexpr wchar_t lockOverlayOnText[] = L"activado";
inline constexpr wchar_t lockOverlayOffText[] = L"desactivado";

// How often the taskbar is re-checked for rebuilds and layout changes.
inline constexpr unsigned taskbarHealthCheckMs = 2000;

// Process-wide names and paths.
inline constexpr wchar_t singleInstanceMutexName[] = L"Local\\Threnody.SingleInstance";
inline constexpr wchar_t appDataFolderName[] = L"Threnody";
inline constexpr wchar_t logFileName[] = L"threnody.log";
inline constexpr std::size_t logMaxBytes = std::size_t{1} << 20;

}  // namespace threnody::config
