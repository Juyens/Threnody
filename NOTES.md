# Threnody — working notes

Personal notes and handoff context. Not user documentation.

## What this is

A minimal media widget embedded in the Windows 11 taskbar: shows the current
track (cover, title, artist), a spectrum visualiser, and transport controls.
Written from scratch in C++, for one user, on one machine.

Deliberately not configurable. Every preference is a compile-time constant.
There is no settings UI, no persistence, no localisation — that is the whole
point, and it is where the bulk of a general-purpose app's code goes.

Named after the metal-adjacent word, not because a funeral lament has anything
to do with a music widget.

## Why not just fork FluentFlyout

There is a fork at `~/Documents/GitHub/FluentFlyout` (C#/WPF) with three local
commits and several uncommitted patches. It works and is in daily use. Threnody
is a from-scratch rewrite because building it is the point, not because the
fork is inadequate.

Keep the fork around: it is working reference code for every hard part here.

## Stack

Decided after weighing C# and C++/Qt. Summary of the reasoning:

- The taskbar embedding trick is pure Win32 and identical in any language.
- Audio capture is *easier* in C++ (no P/Invoke, no NAudio).
- Drawing is *harder* in C++ (DirectWrite and WIC by hand) — accepted cost.
- Qt was rejected: ~40 MB runtime for a single custom-drawn window, and none
  of Qt's widget/layout value applies here.
- Dear ImGui was rejected: immediate mode redraws every frame for a widget
  that runs 24/7, still needs a D3D backend and swapchain underneath, has no
  DirectWrite text shaping (titles are often Japanese), and manual DPI. Its
  advantage is building many controls fast; this widget has almost none.

| Concern | Choice |
|---|---|
| Language | C++20, MSVC |
| Build | CMake + presets, vcpkg manifest mode |
| Window | Raw Win32 |
| Rendering | Direct2D + DirectWrite + WIC |
| Transparency | Per-pixel layered child window, `UpdateLayeredWindow` from a premultiplied DIB (see below: nothing else shows) |
| Media info | C++/WinRT, `Windows.Media.Control` (SMTC) |
| Audio | WASAPI process loopback, no library |
| FFT | kissfft (`kissfft::kissfft-float`, use `kiss_fftr` for real input) |
| Settings UI | Dear ImGui (`imgui[dx11-binding,win32-binding]`), only the settings window |
| Settings file | nlohmann-json, `%LOCALAPPDATA%\Threnody\settings.json` |
| Spotify Web API | C++/WinRT `Windows.Web.Http`, PKCE, refresh token sealed with DPAPI |

All third-party code comes through the vcpkg manifest; all of it is MIT.

## Plan

Each phase is independently verifiable. Do not start a phase before the
previous one visibly works.

1. **A coloured rectangle inside the taskbar.** GDI `FillRect`, no Direct2D.
   This is the riskiest unknown; everything after it is ordinary app code.
2. Direct2D replaces the GDI paint.
3. SMTC: title, artist, cover art.
4. WASAPI process loopback + FFT: the bars.
5. Clicks: toggle the playing app's window, and transport controls.

## How the taskbar embedding works

```cpp
HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);

LONG style = GetWindowLong(hwnd, GWL_STYLE);
style = (style & ~WS_POPUP) | WS_CHILD;   // without dropping WS_POPUP it floats above
SetWindowLong(hwnd, GWL_STYLE, style);

SetParent(hwnd, taskbar);
```

No official API, no sanctioned extension point — Win32 simply allows
reparenting a window into another process's window in the same session.
`Shell_SecondaryTrayWnd` is the class for taskbars on secondary monitors.

Useful extended styles: `WS_EX_NOACTIVATE` (do not steal focus on click),
`WS_EX_TOOLWINDOW` (stay out of alt-tab). Avoid `WS_EX_TRANSPARENT` — that
makes clicks pass *through* the window, which this widget needs to receive.

## Things already learned the hard way

These came out of debugging the C# fork. They apply here verbatim.

**Spotify is the Microsoft Store build.** Its SMTC session id is an AUMID,
`SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify`, not an executable name. Path
parsing breaks on it: `GetFileNameWithoutExtension` reads `.SpotifyMusic_…` as
a file extension and yields `SpotifyAB`, which matches no process. Take the
segment after `!` instead — that is the process name.

**Target the window owner for process loopback.** Spotify runs seven
processes; the one owning the main window is the tree root, so
`PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE` on it covers the children
that actually render audio. Verified by measurement: capturing that tree while
another process played loudly returned RMS 0.000000, and RMS 0.024 with
Spotify itself playing.

**Foreground cannot be read at click time.** Clicking a widget inside the
taskbar hands focus to the taskbar, so `GetForegroundWindow()` no longer names
the app the user was on. Capture it on hover, ignore `Shell_TrayWnd` and
`Shell_SecondaryTrayWnd` when capturing, and update the tracked window after
each toggle so repeated clicks work without moving the pointer.

**A plain child window never shows inside the Windows 11 taskbar.** The
widget embeds, positions and paints (PrintWindow returns the pixels), yet
nothing appears on screen: the taskbar's XAML content is drawn through a
composition tree layered above its GDI surface, so ordinary children paint
underneath it. `WS_EX_LAYERED` with `SetLayeredWindowAttributes` does not
help either. Only a per-pixel layered window (`UpdateLayeredWindow` with
`ULW_ALPHA`) gets its own DWM surface and is composited on top. That is why
the WPF fork works: `AllowsTransparency="True"` is exactly that. Rendering
therefore goes to a 32-bit premultiplied DIB that is pushed with
`UpdateLayeredWindow`; it also gives real transparency for free.

**Writing TaskbarAl directly does not re-lay-out explorer.** The Settings app
also broadcasts a change notification. Writing the value with
`Set-ItemProperty` is still a valid test of the registry watcher, but the icons
will not move until explorer is poked or restarted.

**Spotify reports playback state late.** `TryTogglePlayPauseAsync` returns
`true` immediately and the audio reacts at once, but `PlaybackInfoChanged`
arrives up to ten seconds later. The play/pause glyph is flipped optimistically
on click and the event only confirms it.

**Dear ImGui frames must not be re-entered.** A button handler that pumps
messages (`ShellExecute` does) lets the frame `WM_TIMER` call `render()` again
while a frame is open; ImGui asserts in `NewFrame`, the debug CRT shows a
dialog whose own modal loop re-enters once more, and the process ends in
`abort()`. Button actions are queued and run after `Present`, and `render()`
refuses re-entry. The terminate handler and the symbolised stack trace in the
log are what found this.

**Spotify's SMTC session events are not a reliable feed.** Spotify raises
`MediaPropertiesChanged` four to six times per track over ~8 s (text first,
artwork later), sometimes raises nothing at all while paused and idle, and
`SessionsChanged` fires on track changes with a session object that may or
may not be the same one. Consequences baked into `media/MediaSession`: every
refresh publishes text as soon as it has it and artwork separately (a result
is only dropped if a newer refresh already published that part); handlers
move to the new session object whenever the object differs; `SessionsChanged`
always triggers a full refresh; and a 2 s poll re-reads playback state and
text (artwork only when the text changed) as the safety net. Measured after
that: 1.8 s from the click on "next" to the new title, which is Spotify's
own announcement delay.

**Play/pause is the fastest of two signals.** Spotify's `PlaybackInfoChanged`
arrives anywhere between 50 ms and 14 s after the fact, so it is applied the
moment it comes but never waited for. The captured audio confirms or corrects
it: signal above the floor means playing; silence for 0.7 s means paused
(2 s right after a track change, where the loading gap is silence too).
Spotify keeps streaming silent samples while paused, so a stalled stream is
not a usable cue, and it fades out over a few hundred ms on pause, so audio
is ignored for 600 ms after any announced pause or the tail undoes it.
Measured: play and pause from Spotify reflected in 60-70 ms.

**The taskbar gets rebuilt.** After a session unlock or an explorer restart,
an embedded window can be orphaned and UI Automation queries against the
taskbar can hang rather than fail. Anything that waits on such a query needs
an expiry, or the widget stays broken until the process restarts.

## Environment

- Visual Studio Community 2026, at `C:\Program Files\Microsoft Visual Studio\18\Community`
- CMake generator: `Visual Studio 18 2026`. VS also opens the folder directly.
- vcpkg ships with VS; `VCPKG_ROOT` is already set
- The .NET SDK used by the C# fork lives at `~/.dotnet`, not on `PATH`

## State

All seven phases implemented.

- Message loop, single-instance mutex, file log with exit cause (crash filter
  logs module+offset; debug builds also log CRT assertion text).
- Taskbar layout query (`TaskbarAl`, `TrayNotifyWnd`), registry watcher for
  live alignment changes, re-embedding and tray icon re-adding after an
  explorer restart.
- Per-pixel layered child window fed by a Direct2D DC render target bound to a
  32-bit premultiplied DIB. Grayscale text antialiasing. DirectWrite fallback
  chain routes CJK to Yu Gothic UI / Microsoft YaHei UI / Malgun Gothic.
- SMTC through C++/WinRT: Spotify session picked by the AUMID rule, title,
  artist, cover (decoded and centre-cropped with WIC), play state, and the
  three transport commands from clicks.
- WASAPI process loopback of Spotify's root process tree on its own MTA
  thread, mono mix into a lock-free ring; kissfft real FFT of 4096 samples,
  13 log-spaced bands 40 Hz-8 kHz in dB with a treble tilt, peak-meter
  smoothing, 30 fps timer that only runs while playing or settling.
- Clicks: background/cover toggle the Spotify window; title and artist open
  exact `spotify:track:`/`spotify:artist:` URIs when the Web API is connected
  and the API's track name matches the SMTC title, otherwise
  `spotify:search:`; the visualiser cycles three colour modes: track colour,
  rainbow, and a gradient that ripples hue and lightness around the track
  colour and travels across the bars.
- Lock-key overlay (see the phase 6 notes above).
- Tray icon drawn at runtime (three bars), left click opens settings, right
  click offers settings and quit. Settings window: Win32 + D3D11 + Dear ImGui
  1.92, created and destroyed with the window so nothing renders while it is
  closed; Vercel-like monochrome style, Segoe UI Variable loaded from
  `C:\Windows\Fonts`. Options: start with Windows (HKCU Run), lock-key
  overlay master and per key with a test button, visualiser colour mode,
  Spotify connection. Opens itself once on first run (`setupShown`). A
  "Ver registro" button widens the window with a live view of the log (the
  logger keeps the last 2000 lines in memory): filter, follow, open the file;
  Cascadia Mono with Yu Gothic UI merged in for CJK titles. Button actions
  are deferred to after the ImGui frame; never call anything that pumps
  messages from inside it.
- Spotify Web API: PKCE authorisation in the browser, loopback listener on
  127.0.0.1:38417 for the redirect (one shot, 5 minute timeout, state
  checked), token exchange/refresh over `Windows.Web.Http`, refresh token
  stored DPAPI-sealed in settings.json. The Client ID is the user's own
  Spotify app; the redirect URI to register is shown in the settings window.

Known gaps: the OAuth flow has not been run end-to-end here (it needs the
user's Spotify app; the loopback listener and PKCE are covered by
ThrenodyTests). The Spotify window toggle was verified by the user's own
clicks in the log, not by an automated test.
