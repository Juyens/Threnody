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

kissfft is the only external dependency. Dropping it would remove the need for
vcpkg entirely.

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

Phase 1 done: message loop, single-instance mutex, file log with exit cause,
taskbar layout query (`TaskbarAl`, `TrayNotifyWnd`), registry watcher for live
alignment changes, per-pixel layered child window with re-embedding after an
explorer restart. The rectangle is placed before the tray (left-aligned icons)
or at the left edge (centered icons), with margins.

Rendering currently fills the DIB by hand. Next: phase 2, Direct2D drawing
into that DIB through an `ID2D1DCRenderTarget`.
