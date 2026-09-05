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

// Spectrum visualiser.
inline constexpr int spectrumBarCount = 13;
inline constexpr float spectrumBarWidthDip = 3.0f;
inline constexpr float spectrumBarGapDip = 2.0f;
inline constexpr float spectrumBaselineDip = 2.0f;

// Text. DirectWrite handles shaping; the fallback chain covers CJK titles.
inline constexpr wchar_t fontFamily[] = L"Segoe UI Variable Text";
inline constexpr wchar_t fontFamilyJapanese[] = L"Yu Gothic UI";
inline constexpr wchar_t fontFamilyChinese[] = L"Microsoft YaHei UI";
inline constexpr wchar_t fontFamilyKorean[] = L"Malgun Gothic";
inline constexpr float titleFontSizeDip = 12.5f;
inline constexpr float artistFontSizeDip = 11.0f;

// Colours, straight alpha. Tuned for the dark Windows 11 taskbar.
inline constexpr Color backgroundColor{1.0f, 1.0f, 1.0f, 0.07f};
inline constexpr Color backgroundBorderColor{1.0f, 1.0f, 1.0f, 0.06f};
inline constexpr Color coverPlaceholderColor{1.0f, 1.0f, 1.0f, 0.12f};
inline constexpr Color titleColor{1.0f, 1.0f, 1.0f, 0.95f};
inline constexpr Color artistColor{1.0f, 1.0f, 1.0f, 0.60f};
inline constexpr Color controlColor{1.0f, 1.0f, 1.0f, 0.90f};
inline constexpr Color defaultAccentColor{0.55f, 0.78f, 1.0f, 1.0f};

// How often the taskbar is re-checked for rebuilds and layout changes.
inline constexpr unsigned taskbarHealthCheckMs = 2000;

// Process-wide names and paths.
inline constexpr wchar_t singleInstanceMutexName[] = L"Local\\Threnody.SingleInstance";
inline constexpr wchar_t appDataFolderName[] = L"Threnody";
inline constexpr wchar_t logFileName[] = L"threnody.log";
inline constexpr std::size_t logMaxBytes = std::size_t{1} << 20;

}  // namespace threnody::config
