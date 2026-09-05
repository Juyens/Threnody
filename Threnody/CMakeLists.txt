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
    "src/dsp/SpectrumAnalyzer.h"
    "src/dsp/SpectrumAnalyzer.cpp"
    "src/shell/SpotifyProcess.h"
    "src/shell/SpotifyProcess.cpp"
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
)

find_package(kissfft CONFIG REQUIRED)

target_link_libraries(Threnody PRIVATE
    kissfft::kissfft-float
    d2d1 dwrite windowscodecs dcomp
    windowsapp
    mmdevapi
    user32 gdi32 ole32 shell32 shcore advapi32
)
