add_executable(Threnody WIN32
    "src/Threnody.h"
    "src/Threnody.cpp"
    "src/Threnody.manifest"
    "src/Config.h"
    "src/app/Application.h"
    "src/app/Application.cpp"
    "src/audio/ProcessLoopbackCapture.h"
    "src/audio/ProcessLoopbackCapture.cpp"
    "src/audio/SampleRing.h"
    "src/color/Color.h"
    "src/color/ColorMode.h"
    "src/color/ColorSpace.h"
    "src/color/ColorSpace.cpp"
    "src/color/DominantColor.h"
    "src/color/DominantColor.cpp"
    "src/overlay/KeyboardHook.h"
    "src/overlay/KeyboardHook.cpp"
    "src/overlay/LockKey.h"
    "src/overlay/LockKeyOverlay.h"
    "src/overlay/LockKeyOverlay.cpp"
    "src/render/SvgPath.h"
    "src/render/SvgPath.cpp"
    "src/spotify/LoopbackListener.h"
    "src/spotify/LoopbackListener.cpp"
    "src/spotify/Pkce.h"
    "src/spotify/Pkce.cpp"
    "src/spotify/SpotifyClient.h"
    "src/spotify/SpotifyClient.cpp"
    "src/util/Dpapi.h"
    "src/util/Dpapi.cpp"
    "src/render/IconFactory.h"
    "src/render/IconFactory.cpp"
    "src/shell/Startup.h"
    "src/shell/Startup.cpp"
    "src/tray/SettingsWindow.h"
    "src/tray/SettingsWindow.cpp"
    "src/tray/TrayIcon.h"
    "src/tray/TrayIcon.cpp"
    "src/render/CoverSampler.h"
    "src/render/CoverSampler.cpp"
    "src/settings/Settings.h"
    "src/settings/Settings.cpp"
    "src/shell/Fullscreen.h"
    "src/shell/Fullscreen.cpp"
    "src/shell/SpotifyLinks.h"
    "src/shell/SpotifyLinks.cpp"
    "src/shell/SpotifyWindow.h"
    "src/shell/SpotifyWindow.cpp"
    "src/dsp/SpectrumAnalyzer.h"
    "src/dsp/SpectrumAnalyzer.cpp"
    "src/shell/SpotifyProcess.h"
    "src/shell/SpotifyProcess.cpp"
    "src/i18n/Strings.h"
    "src/interaction/HitTest.h"
    "src/interaction/HitTest.cpp"
    "src/media/MediaSession.h"
    "src/media/MediaSession.cpp"
    "src/media/SourceAppId.h"
    "src/media/SourceAppId.cpp"
    "src/render/Fonts.h"
    "src/render/Fonts.cpp"
    "src/render/Graphics.h"
    "src/render/Graphics.cpp"
    "src/render/LayeredSurface.h"
    "src/render/LayeredSurface.cpp"
    "src/render/WidgetLayout.h"
    "src/render/WidgetLayout.cpp"
    "src/render/WidgetModel.h"
    "src/render/WidgetRenderer.h"
    "src/render/WidgetRenderer.cpp"
    "src/taskbar/RegistryWatcher.h"
    "src/taskbar/RegistryWatcher.cpp"
    "src/taskbar/Taskbar.h"
    "src/taskbar/Taskbar.cpp"
    "src/taskbar/WidgetWindow.h"
    "src/taskbar/WidgetWindow.cpp"
    "src/util/CrashHandler.h"
    "src/util/CrashHandler.cpp"
    "src/util/Log.h"
    "src/util/Log.cpp"
    "src/util/Paths.h"
    "src/util/Paths.cpp"
    "src/util/Result.h"
    "src/util/Result.cpp"
    "src/util/Text.h"
    "src/util/Text.cpp"
    "src/util/Win32.h"
    "src/util/Win32.cpp"
)

target_include_directories(Threnody PRIVATE "src")

target_compile_features(Threnody PRIVATE cxx_std_20)
target_compile_options(Threnody PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc>
)

target_compile_definitions(Threnody PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    THRENODY_VERSION="${PROJECT_VERSION}"
)

find_package(kissfft CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

target_link_libraries(Threnody PRIVATE
    kissfft::kissfft-float
    nlohmann_json::nlohmann_json
    imgui::imgui
    d2d1 dwrite windowscodecs dcomp d3d11 dxgi dwmapi
    windowsapp
    mmdevapi
    user32 gdi32 ole32 shell32 shcore advapi32 crypt32 bcrypt ws2_32 dbghelp
)

# Checks for the screen-free parts: parsing, colour, spectrum, layout,
# settings, PKCE and the loopback listener. Console app, no framework.
add_executable(ThrenodyTests
    "tests/Tests.cpp"
    "src/color/ColorSpace.cpp"
    "src/color/DominantColor.cpp"
    "src/dsp/SpectrumAnalyzer.cpp"
    "src/media/SourceAppId.cpp"
    "src/render/WidgetLayout.cpp"
    "src/settings/Settings.cpp"
    "src/shell/SpotifyLinks.cpp"
    "src/spotify/LoopbackListener.cpp"
    "src/spotify/Pkce.cpp"
    "src/util/Log.cpp"
    "src/util/Result.cpp"
    "src/util/Text.cpp"
)
target_include_directories(ThrenodyTests PRIVATE "src")
target_compile_features(ThrenodyTests PRIVATE cxx_std_20)
target_compile_options(ThrenodyTests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc>
)
target_compile_definitions(ThrenodyTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(ThrenodyTests PRIVATE
    kissfft::kissfft-float
    nlohmann_json::nlohmann_json
    user32 shell32 ole32 bcrypt ws2_32
)
