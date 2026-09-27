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
inline constexpr float controlIconSizeDip = 15.0f;  // Shuffle and volume: Fluent icons, 20-unit box.
inline constexpr float controlActiveDotDip = 3.0f;  // Dot under an active toggle, like Spotify's.

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

// Rainbow colour mode, in OKLCH so every bar looks equally bright: how much
// of the hue circle (degrees) the thirteen bars span at once, how fast the
// sweep travels (full cycle in this many seconds), and the shared lightness
// and chroma. Chroma is kept moderate: pure hues on a dark taskbar glare.
inline constexpr float rainbowHueSpanDegrees = 300.0f;
inline constexpr float rainbowCycleSeconds = 8.0f;
inline constexpr float rainbowLightness = 0.80f;
inline constexpr float rainbowChroma = 0.12f;

// Track-gradient colour mode: one wave travels along the bars at the
// rainbow's speed. At its crest the colour is a lighter, softer tint of the
// cover colour with the hue turned one way; at its trough a deeper shade with
// the hue turned the other way. Everything stays within analogous hues, so
// the bars read as one colour with depth rather than as several colours.
inline constexpr float gradientHueSpreadDegrees = 22.0f;
inline constexpr float gradientLightnessSpread = 0.09f;
inline constexpr float gradientChromaFade = 0.35f;  // Chroma lost at the light crest.
inline constexpr float gradientMinLightness = 0.55f;
inline constexpr float gradientMaxLightness = 0.92f;
inline constexpr float gradientWaveSpan = 1.0f;  // Wave cycles across the thirteen bars.

// Beat pulse: kicks make the widget's border glow in the bars' colour for a
// moment. Detected on the raw energy of the kick drum's range (not on the
// bars, which saturate and smooth away the gaps between hits in loud, dense
// music): a kick is a sudden rise in that energy (spectral flux, in dB) that
// stands out from the rises of the last beatHistoryFrames frames by
// beatSensitivity standard deviations, is at least beatMinRiseDb, happens
// above beatFloorDb, and comes no sooner than beatMinGapMs after the last.
// Hits that stand out more glow brighter; when hits come fast, each glow is
// shortened in proportion so blast beats flash instead of blurring together.
inline constexpr double beatKickLowHz = 40.0;
inline constexpr double beatKickHighHz = 130.0;
inline constexpr std::size_t beatHistoryFrames = 45;  // ~1.5 s of visualiser frames.
inline constexpr float beatSensitivity = 1.2f;
inline constexpr float beatMinRiseDb = 1.5f;
inline constexpr float beatFloorDb = -50.0f;
inline constexpr unsigned beatMinGapMs = 150;
inline constexpr float beatMinPulse = 0.55f;          // Glow of a hit that only just qualifies.
inline constexpr float beatDecayMs = 200.0f;          // Glow time constant for sparse beats.
inline constexpr float beatMinDecayMs = 60.0f;        // ...and the shortest, for the densest.
inline constexpr float beatDecayPerInterval = 0.45f;  // Glow time constant as a share of the beat interval.
inline constexpr float pulseBorderAlpha = 0.80f;
inline constexpr float pulseBorderWidthDip = 1.5f;
inline constexpr float pulseTintAlpha = 0.08f;

// Motion. A new cover turns over like a card; the old title slides up and
// out while the new one comes in from below. Text too long for its column
// scrolls while the pointer is over the widget, after a short pause.
inline constexpr unsigned animationFrameMs = 16;
inline constexpr unsigned coverFlipMs = 450;
inline constexpr unsigned textSlideMs = 320;
inline constexpr float textSlideDip = 9.0f;
inline constexpr unsigned marqueeDelayMs = 500;
inline constexpr float marqueeSpeedDipPerSecond = 30.0f;
inline constexpr float marqueeGapDip = 36.0f;
inline constexpr float marqueeFadeDip = 12.0f;

// Floating widget backdrop: the cover, blurred by shrinking it to a few
// pixels and stretching it back, under a shade that keeps the text legible.
inline constexpr unsigned backdropSamplePx = 8;
inline constexpr Color backdropShadeColor{0.0f, 0.0f, 0.0f, 0.50f};
inline constexpr Color backdropHoverShadeColor{0.0f, 0.0f, 0.0f, 0.38f};

// Cover colour analysis works on a downscaled copy of this many pixels a side.
inline constexpr unsigned coverSampleSize = 48;

// Play/pause shown from the captured audio: sampled this often, silence
// below this RMS, and paused once nothing louder arrived for the hold time.
// Right after a track change the hold is longer, because the gap while the
// next track loads is silence too. Spotify keeps streaming silent samples
// while paused, so silence is the only audio cue there is.
inline constexpr unsigned audioWatchMs = 100;
inline constexpr double audioSilenceRms = 1e-4;
inline constexpr unsigned audioPauseHoldMs = 700;
inline constexpr unsigned audioTrackChangeHoldMs = 2000;
inline constexpr unsigned audioTrackChangeWindowMs = 3000;
inline constexpr unsigned audioPauseGraceMs = 600;  // Spotify's fade-out after a pause is still audible.

// How long to wait before retrying a failed or lost audio capture.
inline constexpr unsigned captureRetryMs = 10000;

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
// While the pointer is over the widget the panel brightens (200 ms fade) and
// the control or text under the pointer gets a rounded highlight.
inline constexpr Color hoverBackgroundColor{1.0f, 1.0f, 1.0f, 0.13f};
inline constexpr Color hoverBorderColor{1.0f, 1.0f, 1.0f, 0.18f};
inline constexpr Color hoverHighlightColor{1.0f, 1.0f, 1.0f, 0.10f};
inline constexpr float hoverHighlightRadiusDip = 4.0f;
inline constexpr float controlHoverInsetDip = 6.0f;   // Vertical inset of a control's highlight.
inline constexpr float textHoverPaddingDip = 3.0f;
inline constexpr unsigned hoverFadeMs = 200;
inline constexpr Color coverPlaceholderColor{1.0f, 1.0f, 1.0f, 0.12f};
inline constexpr Color titleColor{1.0f, 1.0f, 1.0f, 0.95f};
inline constexpr Color artistColor{1.0f, 1.0f, 1.0f, 0.60f};
inline constexpr Color controlColor{1.0f, 1.0f, 1.0f, 0.90f};
inline constexpr Color controlDisabledColor{1.0f, 1.0f, 1.0f, 0.35f};
inline constexpr Color controlActiveColor{0.118f, 0.843f, 0.376f, 1.0f};  // Spotify green, #1ED760.
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

// Volume flyout: opens over the widget's volume button, like Spotify's own
// slider. Changes Spotify's session volume in the Windows mixer.
inline constexpr float volumeFlyoutWidthDip = 220.0f;
inline constexpr float volumeFlyoutHeightDip = 44.0f;
inline constexpr float volumeFlyoutGapDip = 8.0f;          // Between the flyout and the taskbar.
inline constexpr float volumeFlyoutCornerRadiusDip = 8.0f;
inline constexpr float volumeFlyoutIconZoneDip = 40.0f;    // Mute button at the left.
inline constexpr float volumeFlyoutIconSizeDip = 18.0f;
inline constexpr float volumeFlyoutValueZoneDip = 42.0f;   // Percentage at the right.
inline constexpr float volumeFlyoutTrackHeightDip = 4.0f;
inline constexpr float volumeFlyoutThumbRadiusDip = 6.0f;
inline constexpr float volumeFlyoutFontSizeDip = 12.0f;
inline constexpr float volumeStep = 0.05f;                 // Per wheel notch or arrow key.
inline constexpr unsigned volumeFlyoutFadeMs = 120;
inline constexpr Color volumeFlyoutBackgroundColor{0.125f, 0.125f, 0.125f, 0.96f};
inline constexpr Color volumeFlyoutBorderColor{1.0f, 1.0f, 1.0f, 0.09f};
inline constexpr Color volumeFlyoutTrackColor{1.0f, 1.0f, 1.0f, 0.25f};
inline constexpr Color volumeFlyoutFillColor{1.0f, 1.0f, 1.0f, 0.95f};

// Dragging the widget out of the taskbar and back. Out of it the widget
// floats above everything on its own dark panel (the taskbar's translucent
// look would vanish over other windows). Dragged back within the snap
// distance of the taskbar, a ghost of its slot shows where it will dock.
inline constexpr float dockSnapDip = 24.0f;
inline constexpr unsigned dragFrameMs = 8;
inline constexpr unsigned char dragOverDockAlpha = 170;  // The dragged widget, while a drop would dock it.
inline constexpr unsigned dockPreviewFadeMs = 150;
inline constexpr float dockPreviewStrokeDip = 1.5f;
inline constexpr Color dockPreviewFillColor{1.0f, 1.0f, 1.0f, 0.10f};
inline constexpr Color dockPreviewStrokeColor{1.0f, 1.0f, 1.0f, 0.55f};
inline constexpr Color floatingBackgroundColor{0.13f, 0.13f, 0.13f, 0.96f};
inline constexpr Color floatingBorderColor{1.0f, 1.0f, 1.0f, 0.10f};
inline constexpr Color floatingHoverBackgroundColor{0.17f, 0.17f, 0.17f, 0.97f};
inline constexpr Color floatingHoverBorderColor{1.0f, 1.0f, 1.0f, 0.18f};

// Queue peek: resting the pointer on "next" shows what plays next in a
// bubble over the button, after a short pause so passing over it does not.
inline constexpr unsigned queuePeekDelayMs = 350;
inline constexpr float queuePeekWidthDip = 250.0f;
inline constexpr float queuePeekHeightDip = 64.0f;
inline constexpr float queuePeekPaddingDip = 10.0f;
inline constexpr float queuePeekCoverDip = 44.0f;
inline constexpr float queuePeekGapDip = 8.0f;
inline constexpr float queuePeekLabelSizeDip = 10.5f;
inline constexpr float queuePeekTitleSizeDip = 13.0f;
inline constexpr float queuePeekSubtitleSizeDip = 11.5f;
inline constexpr unsigned queuePeekFadeMs = 140;
// Right after a skip the Web API still describes the previous track; a queue
// read then is asked again after this long, up to this many times.
inline constexpr unsigned queueRetryMs = 600;
inline constexpr unsigned queueRetryLimit = 5;

// Wheel over the widget: Spotify's volume in steps of volumeStep per notch.
// The visualiser shows the level for a moment instead of the bars.
inline constexpr unsigned volumeOsdHoldMs = 900;
inline constexpr unsigned volumeOsdFadeMs = 250;
inline constexpr float volumeOsdTrackHeightDip = 3.0f;

// SMTC reports Spotify's state seconds late. After a shuffle click the
// widget shows the new state and ignores a contrary report for this long.
inline constexpr unsigned shuffleConfirmHoldMs = 15000;

// Settings window (Dear ImGui), client area in DIPs.
inline constexpr int settingsWindowWidthDip = 460;
inline constexpr int settingsWindowHeightDip = 740;
inline constexpr int settingsLogPanelWidthDip = 640;  // Added to the right when the live log is shown.
inline constexpr wchar_t settingsMonoFontFile[] = L"CascadiaMono.ttf";
inline constexpr wchar_t settingsMonoFontFallback[] = L"consola.ttf";
inline constexpr wchar_t settingsCjkFontFile[] = L"YuGothM.ttc";  // Merged into the log font for titles.

// Spotify Web API (authorisation code with PKCE, no client secret). The
// redirect URI must match the one registered in the Spotify app; the port is
// fixed so it can be typed into the dashboard once.
inline constexpr unsigned short spotifyRedirectPort = 38417;
inline constexpr wchar_t spotifyRedirectUri[] = L"http://127.0.0.1:38417/callback";
inline constexpr wchar_t spotifyScopes[] = L"user-read-currently-playing user-read-playback-state";
// After a quick skip the Web API can still report the previous track. Asking
// again on the 2 s health tick, this many times per track, lets exact links
// and the artwork fallback catch up.
inline constexpr unsigned spotifyLinksRetryLimit = 3;

// How often the taskbar is re-checked for rebuilds and layout changes.
inline constexpr unsigned taskbarHealthCheckMs = 2000;

// Process-wide names and paths.
inline constexpr wchar_t singleInstanceMutexName[] = L"Local\\Threnody.SingleInstance";
inline constexpr wchar_t appDataFolderName[] = L"Threnody";
inline constexpr wchar_t logFileName[] = L"threnody.log";
inline constexpr std::size_t logMaxBytes = std::size_t{1} << 20;
inline constexpr std::size_t logRecentLines = 2000;  // Kept in memory for the live viewer.

}  // namespace threnody::config
