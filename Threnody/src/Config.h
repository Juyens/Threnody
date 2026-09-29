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
inline constexpr float backgroundCornerRadiusDip = 8.0f;
inline constexpr float coverCornerRadiusDip = 6.0f;
inline constexpr float textMaxWidthDip = 170.0f;
inline constexpr float textLineGapDip = 1.0f;
inline constexpr float controlButtonWidthDip = 22.0f;
inline constexpr float controlIconSizeDip = 16.0f;  // The Lucide icons' 24-unit box.
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

// Wave style: the waveform in this many points across the visualiser (see
// dsp::Oscilloscope). The scale follows the loudest recent point, falling
// by waveformPeakFall a frame; each frame moves the line waveformSmoothing
// of the way to the new shape, and silence flattens it by waveformDecay.
inline constexpr int waveformPoints = 48;
inline constexpr float waveformPeakFall = 0.97f;
inline constexpr float waveformMinPeak = 0.01f;
inline constexpr float waveformSmoothing = 0.6f;
inline constexpr float waveformDecay = 0.8f;
inline constexpr float waveformStrokeDip = 1.6f;

// Curve style: the line over the filled area, and how opaque the area is.
inline constexpr float curveStrokeDip = 1.6f;
inline constexpr float curveFillAlpha = 0.35f;

// Retro LED style: segments stacked in each column, the unlit ones faintly
// shown; the highest level reached lingers as a peak segment, then falls.
inline constexpr float ledSegmentDip = 2.0f;
inline constexpr float ledGapDip = 1.0f;
inline constexpr float ledUnlitAlpha = 0.14f;
inline constexpr float ledPeakHoldMs = 400.0f;
inline constexpr float ledPeakFallPerSecond = 1.2f;  // Share of the column height.

// Spectrum on the vinyl: with it the record shrinks to vinylRingDiscShare of
// the cover square and the bands stand around it as rays, bass at the
// bottom and treble at the top, mirrored left and right.
inline constexpr float vinylRingDiscShare = 0.86f;
inline constexpr int vinylRingRays = 96;
inline constexpr float vinylRingRayDip = 2.6f;  // Stroke width.
inline constexpr float vinylRingGapDip = 3.0f;  // Between the rim and the rays.

// Wavy progress line: while playing, the played part of the line ripples
// (a sine travelling along it, taller with louder music); on pause it
// settles flat. Amplitude and wavelength for the bar, then for the card.
inline constexpr float progressWaveAmplitudeDip = 1.2f;
inline constexpr float progressWaveLengthDip = 9.0f;
inline constexpr float cardProgressWaveAmplitudeDip = 1.8f;
inline constexpr float cardProgressWaveLengthDip = 12.0f;
inline constexpr float progressWaveCyclesPerSecond = 1.4f;
inline constexpr float progressWaveSettleSeconds = 0.4f;

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

// Card: the floating widget has two sizes, the bar and a vertical player
// card of one standard size (cover on top, then text and controls). Pulling
// an edge (within resizeEdgeDip) by cardSnapDeltaDip in height switches
// between them at once, either way; there are no sizes in between. The
// card's width sets everything: the cover fills it bar the padding, and the
// height follows from what sits under the cover.
inline constexpr float cardWidthDip = 300.0f;
inline constexpr float cardSnapDeltaDip = 40.0f;
inline constexpr float cardPaddingDip = 18.0f;
inline constexpr float cardCornerRadiusDip = 14.0f;
inline constexpr float cardCoverCornerRadiusDip = 10.0f;
inline constexpr float cardGapDip = 14.0f;
inline constexpr float cardControlsHeightDip = 40.0f;
inline constexpr float cardControlMaxWidthDip = 56.0f;
inline constexpr float cardControlScale = 1.5f;  // Icons, relative to the bar's.
inline constexpr float resizeEdgeDip = 7.0f;
// "Take out of the taskbar" from the menu: the widget floats this far above it.
inline constexpr int undockLiftDip = 12;

// Vinyl mode (card only, optional): the cover becomes the label of a record
// that turns at 33 1/3 rpm while playing, speeding up and running down like
// a platter, under a tonearm that lowers onto the grooves when the music
// starts, tracks inward as the song plays, and lifts off on pause. The
// highlights on the vinyl stay put while it turns, as they do on a real one.
inline constexpr float vinylRpm = 100.0f / 3.0f;
inline constexpr float vinylSpinUpSeconds = 0.6f;
inline constexpr float vinylSpinDownSeconds = 1.4f;
inline constexpr float vinylArmSeconds = 0.5f;
inline constexpr float vinylLabelShare = 0.68f;       // Label radius over the record's.
inline constexpr float vinylGrooveSpacingDip = 2.5f;
inline constexpr float vinylHoleDip = 4.0f;
inline constexpr float vinylArmPivotInsetDip = 16.0f;  // From the cover square's top-right corner.
inline constexpr float vinylArmLengthShare = 0.9f;     // Of the record's radius.
inline constexpr float vinylArmOuterShare = 0.92f;     // Needle at the start of a song, of the radius...
inline constexpr float vinylArmInnerShare = 0.15f;     // ...and at its end, of the grooves' width past the label.
inline constexpr float vinylArmRestDeg = 9.0f;         // Swung out past the rim when lifted.
inline constexpr Color vinylDiscInnerColor{0.10f, 0.10f, 0.11f, 1.0f};
inline constexpr Color vinylDiscOuterColor{0.04f, 0.04f, 0.045f, 1.0f};
inline constexpr Color vinylGrooveColor{1.0f, 1.0f, 1.0f, 0.045f};
inline constexpr Color vinylTrackGapColor{1.0f, 1.0f, 1.0f, 0.08f};
inline constexpr Color vinylRimColor{1.0f, 1.0f, 1.0f, 0.10f};
inline constexpr Color vinylSheenColor{1.0f, 1.0f, 1.0f, 0.035f};  // Per layer of the highlight.
inline constexpr Color vinylShadowColor{0.0f, 0.0f, 0.0f, 0.09f};  // Per ring of the record's shadow.
inline constexpr Color vinylArmColor{0.84f, 0.84f, 0.86f, 1.0f};
inline constexpr Color vinylArmDarkColor{0.42f, 0.42f, 0.45f, 1.0f};
inline constexpr Color vinylArmBaseColor{0.17f, 0.17f, 0.19f, 1.0f};
inline constexpr Color vinylArmShadowColor{0.0f, 0.0f, 0.0f, 0.35f};

// Floating widget backdrop: a wash of the cover's colours, like the blurred
// artwork behind Apple Music's player. A band across the middle of the
// cover (taller than the panel's own proportion, so there is more colour to
// blend) is scaled to backdropSampleWidthPx wide, blurred with three box
// passes of backdropBlurPx (close to a Gaussian), and stretched over the
// panel under a shade that keeps the text legible.
inline constexpr unsigned backdropSampleWidthPx = 128;
inline constexpr float backdropBandStretch = 3.0f;  // Band height over the panel's own proportion.
inline constexpr int backdropBlurPx = 10;
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
inline constexpr float cardTitleFontSizeDip = 17.0f;
inline constexpr float cardArtistFontSizeDip = 13.5f;

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
inline constexpr Color controlOffColor{0.655f, 0.655f, 0.655f, 0.95f};  // A toggle that is off, as Spotify greys it.
// The panel takes a hint of the cover's colour, its border a little more;
// a thin rule sets the visualiser apart from the controls.
inline constexpr float panelTintAlpha = 0.10f;
inline constexpr float panelHoverTintAlpha = 0.14f;
inline constexpr float panelBorderTintAlpha = 0.22f;
inline constexpr float separatorHeightShare = 0.55f;  // Of the widget's height.
inline constexpr Color separatorColor{1.0f, 1.0f, 1.0f, 0.12f};
// Song progress: a thin line along the bottom edge, in the beat glow's
// colour over a faint track. SMTC reports the position every few seconds; in
// between it runs on the clock. The track sits on a dark base so neither the
// beat glow along the border nor the hover brightening shows through it and
// blurs where the progress ends.
inline constexpr float progressHeightDip = 2.0f;
// Seeking: the bottom progressHitDip of the widget belong to the line.
// Pointed at, it thickens, shows a knob at the position, and the time under
// the pointer takes the visualiser's place (below the controls on the card);
// a click or a drag along it moves the song there.
inline constexpr float progressHitDip = 6.0f;
inline constexpr float progressActiveHeightDip = 4.0f;
inline constexpr float progressKnobRadiusDip = 4.5f;
inline constexpr Color progressBaseColor{0.0f, 0.0f, 0.0f, 0.55f};
inline constexpr Color progressTrackColor{1.0f, 1.0f, 1.0f, 0.12f};
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

// Popups (the up-next bubble) and level indicators share these.
inline constexpr float popupCornerRadiusDip = 8.0f;
inline constexpr Color popupBackgroundColor{0.125f, 0.125f, 0.125f, 0.96f};
inline constexpr Color popupBorderColor{1.0f, 1.0f, 1.0f, 0.09f};
inline constexpr Color levelTrackColor{1.0f, 1.0f, 1.0f, 0.25f};
inline constexpr Color levelFillColor{1.0f, 1.0f, 1.0f, 0.95f};
inline constexpr float volumeStep = 0.05f;  // Spotify's mixer volume per wheel notch.

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

// Info bubbles: resting the pointer on "next" shows what plays next, on the
// title the song's details, on the artist the artist's; after a short pause,
// so passing over them does not. The image matches the text block's height
// within the limits.
inline constexpr unsigned bubbleDelayMs = 350;
inline constexpr unsigned bubbleTextDelayMs = 500;  // Title and artist: longer, they are big targets.
inline constexpr float bubbleWidthDip = 290.0f;
inline constexpr float bubblePaddingDip = 10.0f;
inline constexpr float bubbleImageMinDip = 44.0f;
inline constexpr float bubbleImageMaxDip = 72.0f;
inline constexpr float bubbleGapDip = 8.0f;
inline constexpr float bubbleLabelSizeDip = 10.5f;
inline constexpr float bubbleTitleSizeDip = 13.0f;
inline constexpr float bubbleLineSizeDip = 11.5f;
inline constexpr unsigned bubbleFadeMs = 140;
inline constexpr std::size_t bubbleGenres = 3;  // At most this many of the artist's genres.
// Right after a skip the Web API still describes the previous track; a queue
// read then is asked again after this long, up to this many times.
inline constexpr unsigned queueRetryMs = 600;
inline constexpr unsigned queueRetryLimit = 5;

// Wheel over the widget: Spotify's volume in steps of volumeStep per notch.
// The visualiser shows the level for a moment instead of the bars.
inline constexpr unsigned volumeOsdHoldMs = 900;
inline constexpr unsigned volumeOsdFadeMs = 250;
inline constexpr float volumeOsdTrackHeightDip = 3.0f;
// On the card, which has no visualiser, the level shows in a translucent
// capsule over the bottom of the cover: speaker, level, percentage.
inline constexpr float cardOsdHeightDip = 30.0f;
inline constexpr float cardOsdWidthShare = 0.72f;  // Of the cover's width.
inline constexpr float cardOsdMarginDip = 12.0f;   // Above the cover's bottom edge.
inline constexpr float cardOsdIconDip = 16.0f;
inline constexpr float cardOsdValueDip = 30.0f;    // Room for "100".
inline constexpr float cardOsdLiftDip = 6.0f;      // Rises this much as it appears.
inline constexpr Color cardOsdBackgroundColor{0.06f, 0.06f, 0.06f, 0.72f};

// Spotify applies a shuffle or repeat request at once and SMTC reports the
// new state within about 50 ms. A contrary report inside this window after a
// click predates it and is ignored.
inline constexpr unsigned toggleConfirmHoldMs = 3000;
// Clicks on the same toggle closer together than this are dropped: racing
// Spotify before it has applied the last one leaves the two out of step.
inline constexpr unsigned toggleDebounceMs = 350;
// Smart shuffle only shows through the Web API's player state, which trails
// SMTC by one to two seconds. It is read on track changes, twice after a
// shuffle click or a shuffle change made in Spotify (at these delays), and
// every this many health ticks while a session exists. Right after a click,
// a smart shuffle report is not trusted for smartShuffleSettleMs.
inline constexpr unsigned playerStateRefreshTicks = 2;
inline constexpr unsigned playerStateRecheckFirstMs = 400;
inline constexpr unsigned playerStateRecheckSecondMs = 1600;
inline constexpr unsigned smartShuffleSettleMs = 2500;
// The smart shuffle icon: the shuffle arrows shrunk toward the lower right,
// a sparkle in the upper left.
inline constexpr float smartShuffleArrowsShare = 0.82f;
inline constexpr float smartShuffleSparkleShare = 0.48f;

// Settings window (Dear ImGui), client area in DIPs.
inline constexpr int settingsWindowWidthDip = 460;
inline constexpr int settingsWindowHeightDip = 804;
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
