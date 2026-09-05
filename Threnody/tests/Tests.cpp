// Checks for the parts of Threnody that do not need a taskbar, a Spotify
// session or a screen. Plain functions and a counter; no framework.

#include "Config.h"
#include "color/DominantColor.h"
#include "dsp/SpectrumAnalyzer.h"
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
    check(narrow.cover.left < narrow.title.left && narrow.title.right <= narrow.previous.left &&
              narrow.next.right <= narrow.visualizer.left,
          "zones are laid out left to right");
    check(wide.title.width() <= threnody::config::textMaxWidthDip, "text column is clamped");
}

void testSettingsRoundTrip() {
    using namespace threnody;
    const std::filesystem::path file = std::filesystem::temp_directory_path() / L"threnody-test-settings.json";
    settings::Settings original;
    original.startWithWindows = true;
    original.lockKeys.numLock = false;
    original.colorMode = ColorMode::Rainbow;
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
    testSpectrum();
    testLayout();
    testSettingsRoundTrip();
    testLoopbackListener();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
