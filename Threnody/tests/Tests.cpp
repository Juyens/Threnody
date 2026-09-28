// Checks for the parts of Threnody that do not need a taskbar, a Spotify
// session or a screen. Plain functions and a counter; no framework.

#include "Config.h"
#include "color/ColorSpace.h"
#include "color/DominantColor.h"
#include "dsp/BeatDetector.h"
#include "dsp/SpectrumAnalyzer.h"
#include "interaction/HitTest.h"
#include "media/SourceAppId.h"
#include "render/WidgetLayout.h"
#include "settings/Settings.h"
#include "shell/SpotifyLinks.h"
#include "spotify/LoopbackListener.h"
#include "spotify/Pkce.h"
#include "util/Log.h"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <future>
#include <numbers>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

void testSourceAppId() {
    using threnody::media::isSpotifySource;
    using threnody::media::processNameFromSourceAppId;
    check(processNameFromSourceAppId(L"SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify") == L"Spotify", "AUMID -> Spotify");
    check(processNameFromSourceAppId(L"Spotify.exe") == L"Spotify", "exe name -> Spotify");
    check(processNameFromSourceAppId(L"C:\\Apps\\Spotify.EXE") == L"Spotify", "path -> Spotify");
    check(processNameFromSourceAppId(L"Chrome.exe") == L"Chrome", "other exe");
    check(isSpotifySource(L"SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"), "AUMID is Spotify");
    check(!isSpotifySource(L"Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic"), "Zune is not Spotify");
}

void testPkce() {
    // RFC 7636, appendix B.
    const auto challenge = threnody::spotify::pkce::challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
    check(challenge.ok(), "challenge computes");
    check(challenge.ok() && *challenge == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", "challenge matches RFC vector");
    const auto a = threnody::spotify::pkce::randomToken();
    const auto b = threnody::spotify::pkce::randomToken();
    check(a.ok() && b.ok() && *a != *b && a->size() == 64, "random tokens differ and are 64 chars");
}

void testPercentEncode() {
    using threnody::shell::percentEncode;
    check(percentEncode(L"ヨルシカ") == L"%E3%83%A8%E3%83%AB%E3%82%B7%E3%82%AB", "CJK encodes as UTF-8 bytes");
    check(percentEncode(L"a b&c") == L"a%20b%26c", "space and ampersand");
    check(percentEncode(L"A-Z_0.9~") == L"A-Z_0.9~", "unreserved untouched");
}

void testDominantColor() {
    using threnody::Color;
    using threnody::color::dominantColor;
    std::vector<std::uint32_t> pixels(64 * 64);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = i % 4 == 0 ? 0xFF202020u : 0xFFC02020u;  // BGRA: mostly red-ish with some dark grey.
    }
    const Color red = dominantColor(pixels, Color{0, 0, 1, 1});
    check(red.r > red.g && red.r > red.b, "dominant colour is red");
    check(red.r > 0.5f, "dominant colour is bright enough");

    std::fill(pixels.begin(), pixels.end(), 0xFF303030u);
    const Color fallback = dominantColor(pixels, Color{0, 0, 1, 1});
    check(fallback.b == 1.0f && fallback.r == 0.0f, "grey cover falls back");
}

void testOklch() {
    using threnody::Color;
    using threnody::color::fromOklch;
    using threnody::color::toOklch;
    const Color sky{0.35f, 0.63f, 0.90f, 1.0f};
    const auto lch = toOklch(sky);
    const Color back = fromOklch(lch);
    const auto close = [](float a, float b) { return std::abs(a - b) < 0.004f; };
    check(close(back.r, sky.r) && close(back.g, sky.g) && close(back.b, sky.b), "OKLCH round-trips");
    check(lch.h > 220.0f && lch.h < 270.0f, "sky blue lands on the blue hue");

    const auto white = toOklch(Color{1.0f, 1.0f, 1.0f, 1.0f});
    check(white.l > 0.99f && white.c < 0.01f, "white is achromatic at L=1");

    // Chroma beyond what sRGB can show comes back inside the gamut with the
    // hue kept, rather than clipped into a different colour.
    const Color loud = fromOklch({0.80f, 0.40f, 145.0f});
    const auto loudLch = toOklch(loud);
    check(loud.r >= 0.0f && loud.g <= 1.0f && std::abs(loudLch.h - 145.0f) < 3.0f, "out-of-gamut keeps its hue");
    check(std::abs(loudLch.l - 0.80f) < 0.02f, "out-of-gamut keeps its lightness");
}

void testSpectrum() {
    using threnody::dsp::SpectrumAnalyzer;
    SpectrumAnalyzer analyzer{44100};
    std::array<float, SpectrumAnalyzer::fftSize> samples{};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * 1000.0f * static_cast<float>(i) / 44100.0f);
    }
    for (int i = 0; i < 20; ++i) {
        analyzer.analyze(samples);
    }
    const auto& bands = analyzer.bands();
    // Band index of 1 kHz: log position between 40 Hz and 8 kHz.
    const double position = std::log(1000.0 / threnody::config::spectrumMinHz) /
                            std::log(threnody::config::spectrumMaxHz / threnody::config::spectrumMinHz);
    const std::size_t expected = static_cast<std::size_t>(position * SpectrumAnalyzer::bandCount);
    std::size_t loudest = 0;
    for (std::size_t i = 1; i < bands.size(); ++i) {
        if (bands[i] > bands[loudest]) {
            loudest = i;
        }
    }
    check(loudest == expected, "1 kHz sine lands in its band");
    check(bands[loudest] > 0.3f, "tone reaches a visible level");
    for (int i = 0; i < 200; ++i) {
        analyzer.decay();
    }
    check(analyzer.idle(), "bars settle to the baseline");
}

void testLayout() {
    using threnody::render::WidgetLayout;
    const WidgetLayout narrow = WidgetLayout::compute(40.0f, 50.0f, 16.0f, 30.0f, 14.0f);
    const WidgetLayout wide = WidgetLayout::compute(40.0f, 900.0f, 16.0f, 30.0f, 14.0f);
    check(narrow.width < wide.width, "wider text widens the widget");
    check(wide.width <= static_cast<float>(threnody::config::widgetMaxWidthDip), "width is capped");
    check(narrow.cover.left < narrow.title.left && narrow.title.right <= narrow.shuffle.left &&
              narrow.shuffle.right <= narrow.previous.left && narrow.next.right <= narrow.repeat.left &&
              narrow.repeat.right <= narrow.visualizer.left,
          "zones are laid out left to right");
    check(wide.title.width() <= threnody::config::textMaxWidthDip, "text column is clamped");
    check(threnody::interaction::hitTest(narrow, narrow.width / 2.0f, narrow.height - 1.0f) ==
              threnody::interaction::Zone::Progress,
          "the bottom edge seeks");
    check(threnody::interaction::hitTest(narrow, narrow.playPause.left + 1.0f, narrow.height / 2.0f) ==
              threnody::interaction::Zone::PlayPause,
          "the middle of a control is still the control");

    const WidgetLayout card = WidgetLayout::computeCard(22.0f, 18.0f);
    check(card.card && card.width == threnody::config::cardWidthDip, "card has the standard width");
    check(card.cover.width() == card.cover.height() &&
              card.cover.width() == card.width - 2.0f * threnody::config::cardPaddingDip,
          "card cover is a square spanning the card");
    check(card.cover.bottom <= card.title.top && card.artist.bottom <= card.previous.top &&
              card.repeat.bottom <= card.height && card.visualizer.width() == 0.0f,
          "card stacks cover, text and controls, with no visualiser");
}

void testBeatDetector() {
    using threnody::dsp::BeatDetector;
    constexpr float frameMs = 33.0f;
    // Counts beats over `frames` frames of a kick every `period` frames that
    // lifts the level from `base` by `lift` dB for one frame.
    const auto run = [&](float base, float lift, int period, int frames) {
        BeatDetector detector;
        int beats = 0;
        float previous = 0.0f;
        for (int i = 0; i < frames; ++i) {
            const float level = period > 0 && i % period == 0 ? base + lift : base;
            const float pulse = detector.update(level, frameMs);
            beats += pulse > previous + 0.2f ? 1 : 0;
            previous = pulse;
        }
        return beats;
    };
    check(run(-20.0f, 0.0f, 0, 200) == 0, "a steady level does not pulse");
    const int regular = run(-20.0f, 12.0f, 15, 300);  // ~120 BPM, 20 kicks
    check(regular >= 17 && regular <= 20, "each kick of a regular beat pulses");
    const int loud = run(-3.0f, 6.0f, 13, 260);  // Brickwall master: loud floor, 20 kicks
    check(loud >= 17 && loud <= 20, "kicks pulse even when the bass never gets quiet");
    const int dense = run(-10.0f, 8.0f, 2, 300);  // A hit every 66 ms
    check(dense > 0 && static_cast<float>(dense) <= 300.0f * frameMs / threnody::config::beatMinGapMs + 1.0f,
          "blast beats pulse, but no faster than the minimum gap");
    check(run(-90.0f, 12.0f, 15, 300) == 0, "near silence does not pulse");
}

// Kicks (a decaying 55 Hz thump every 500 ms) under a loud, steady mid tone,
// through the real analyser at the visualiser's frame rate.
void testKickDetection() {
    using threnody::dsp::SpectrumAnalyzer;
    constexpr int rate = 48000;
    constexpr int hop = rate * 33 / 1000;
    constexpr float pi = 3.14159265f;
    SpectrumAnalyzer analyzer(rate);
    threnody::dsp::BeatDetector detector;
    std::vector<float> signal(static_cast<std::size_t>(rate * 10));
    for (std::size_t n = 0; n < signal.size(); ++n) {
        const float t = static_cast<float>(n) / rate;
        const float sinceKick = std::fmod(t, 0.5f);
        const float kick = 0.8f * std::exp(-sinceKick / 0.08f) * std::sin(2.0f * pi * 55.0f * sinceKick);
        signal[n] = kick + 0.4f * std::sin(2.0f * pi * 1000.0f * t);
    }
    int beats = 0;
    float previous = 0.0f;
    for (std::size_t end = SpectrumAnalyzer::fftSize; end <= signal.size(); end += hop) {
        analyzer.analyze(std::span<const float, SpectrumAnalyzer::fftSize>(signal.data() + end - SpectrumAnalyzer::fftSize,
                                                                           SpectrumAnalyzer::fftSize));
        const float pulse = detector.update(analyzer.kickDb(), 33.0f);
        beats += pulse > previous + 0.2f ? 1 : 0;
        previous = pulse;
    }
    check(beats >= 17 && beats <= 20, "kicks under a loud tone pulse once each");
}

void testSettingsRoundTrip() {
    using namespace threnody;
    const std::filesystem::path file = std::filesystem::temp_directory_path() / L"threnody-test-settings.json";
    settings::Settings original;
    original.startWithWindows = true;
    original.lockKeys.numLock = false;
    original.colorMode = ColorMode::Rainbow;
    original.floating = true;
    original.floatingX = -1200;
    original.floatingY = 340;
    original.card = true;
    original.spotifyClientId = "abc";
    original.spotifyRefreshTokenProtected = "sealed";
    check(settings::save(original, file).ok(), "settings save");
    const settings::Settings loaded = settings::load(file);
    check(loaded == original, "settings round-trip");
    std::filesystem::remove(file);
    check(settings::load(file) == settings::Settings{}, "missing file yields defaults");
}

void testLoopbackListener() {
    using threnody::spotify::LoopbackListener;
    std::promise<LoopbackListener::Redirect> promise;
    auto future = promise.get_future();
    constexpr unsigned short port = 38999;
    LoopbackListener listener{port, "/callback", 10, [&](LoopbackListener::Redirect r) { promise.set_value(std::move(r)); }};

    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
    std::string response;
    for (int attempt = 0; attempt < 20 && response.empty(); ++attempt) {
        Sleep(50);
        const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{.sin_family = AF_INET, .sin_port = htons(port)};
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
            const std::string request = "GET /callback?code=ab%20c&state=xyz HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
            send(s, request.data(), static_cast<int>(request.size()), 0);
            std::array<char, 4096> buffer{};
            int received = 0;
            while ((received = recv(s, buffer.data(), static_cast<int>(buffer.size()), 0)) > 0) {
                response.append(buffer.data(), static_cast<std::size_t>(received));
            }
        }
        closesocket(s);
    }
    WSACleanup();

    check(response.starts_with("HTTP/1.1 200"), "callback answered 200");
    check(response.find("Threnody") != std::string::npos, "callback page mentions Threnody");
    check(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "handler invoked");
    if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const LoopbackListener::Redirect redirect = future.get();
        check(redirect.error.empty(), "no listener error");
        check(redirect.query.at("code") == "ab c" && redirect.query.at("state") == "xyz", "query decoded");
    }
}

}  // namespace

int main() {
    testSourceAppId();
    testPkce();
    testPercentEncode();
    testDominantColor();
    testOklch();
    testSpectrum();
    testLayout();
    testBeatDetector();
    testKickDetection();
    testSettingsRoundTrip();
    testLoopbackListener();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
