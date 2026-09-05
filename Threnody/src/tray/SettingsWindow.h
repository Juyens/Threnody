#pragma once

#include "settings/Settings.h"
#include "util/Result.h"
#include "util/Win32.h"

#include <unknwn.h>
#include <d3d11.h>
#include <dxgi.h>
#include <winrt/base.h>

#include <array>
#include <functional>
#include <string>

struct ImGuiContext;
struct ImFont;

namespace threnody::tray {

// What the settings window can ask the application to do. Every change is
// applied immediately; there is no OK button.
struct SettingsActions {
    std::function<void(const settings::Settings&)> onChanged;
    std::function<void()> onTestOverlay;
    std::function<void(std::string clientId)> onConnectSpotify;
    std::function<void()> onDisconnectSpotify;
    std::function<void()> onQuit;
};

struct SpotifyStatus {
    bool connected{false};
    std::string detail;  // Human-readable state or error, UTF-8.
};

// The Dear ImGui settings window. Everything (window, D3D11 device, ImGui
// context) exists only while it is open, so a closed window costs nothing;
// while open it renders at ~60 fps from a timer.
class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, SettingsActions actions);
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    [[nodiscard]] Result<void> open(const settings::Settings& current, HICON icon);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return static_cast<bool>(m_hwnd); }

    // Keep the shown values in step with changes made elsewhere (a click on
    // the visualiser, a finished Spotify authorisation).
    void setSettings(const settings::Settings& current);
    void setSpotifyStatus(SpotifyStatus status);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] Result<void> createDevice();
    [[nodiscard]] Result<void> createRenderTarget();
    void destroyDevice() noexcept;
    void setupImGui();
    void applyStyle() const;
    void render();
    void drawContents();
    void sectionLabel(const char* text) const;
    void changed();

    HINSTANCE m_instance{};
    SettingsActions m_actions;
    win32::WindowClass m_class;
    win32::unique_hwnd m_hwnd;
    UINT m_dpi{96};

    winrt::com_ptr<ID3D11Device> m_device;
    winrt::com_ptr<ID3D11DeviceContext> m_context;
    winrt::com_ptr<IDXGISwapChain> m_swapChain;
    winrt::com_ptr<ID3D11RenderTargetView> m_renderTarget;

    ImGuiContext* m_imgui{};
    ImFont* m_bodyFont{};
    ImFont* m_headingFont{};
    ImFont* m_smallFont{};

    settings::Settings m_settings;
    SpotifyStatus m_spotify;
    std::array<char, 128> m_clientId{};
};

}  // namespace threnody::tray
