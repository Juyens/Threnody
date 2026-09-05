#include "tray/SettingsWindow.h"

#include "Config.h"
#include "util/Log.h"
#include "util/Text.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace threnody::tray {
namespace {

constexpr wchar_t settingsClassName[] = L"ThrenodySettings";
constexpr UINT_PTR frameTimerId = 1;
constexpr unsigned frameMs = 16;

// Vercel-like palette: near-black ground, hairline borders a step lighter,
// monochrome accents.
constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) noexcept {
    return ImVec4{r / 255.0f, g / 255.0f, b / 255.0f, a};
}
constexpr ImVec4 colorGround = rgb(10, 10, 10);
constexpr ImVec4 colorSurface = rgb(17, 17, 17);
constexpr ImVec4 colorSurfaceHover = rgb(26, 26, 26);
constexpr ImVec4 colorSurfaceActive = rgb(38, 38, 38);
constexpr ImVec4 colorBorder = rgb(38, 38, 38);
constexpr ImVec4 colorText = rgb(237, 237, 237);
constexpr ImVec4 colorMuted = rgb(161, 161, 161);
constexpr ImVec4 colorDisabled = rgb(92, 92, 92);
constexpr ImVec4 colorPrimary = rgb(237, 237, 237);
constexpr ImVec4 colorPrimaryHover = rgb(204, 204, 204);
constexpr ImVec4 colorOnPrimary = rgb(10, 10, 10);

std::filesystem::path systemFont(const wchar_t* file) {
    std::array<wchar_t, MAX_PATH> windows{};
    GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    return std::filesystem::path{windows.data()} / L"Fonts" / file;
}

// Buttons drawn as white pills with dark text (primary) or as outlined dark
// pills (secondary), both Vercel staples.
bool primaryButton(const char* label, ImVec2 size = {}) {
    ImGui::PushStyleColor(ImGuiCol_Button, colorPrimary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colorPrimaryHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colorPrimaryHover);
    ImGui::PushStyleColor(ImGuiCol_Text, colorOnPrimary);
    ImGui::PushStyleColor(ImGuiCol_Border, colorPrimary);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(5);
    return pressed;
}

bool secondaryButton(const char* label, ImVec2 size = {}) {
    ImGui::PushStyleColor(ImGuiCol_Button, colorGround);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colorSurfaceHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colorSurfaceActive);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

}  // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, SettingsActions actions)
    : m_instance(instance),
      m_actions(std::move(actions)),
      m_class(WNDCLASSEXW{
          .cbSize = sizeof(WNDCLASSEXW),
          .style = CS_HREDRAW | CS_VREDRAW,
          .lpfnWndProc = &SettingsWindow::windowProc,
          .hInstance = instance,
          .hCursor = LoadCursorW(nullptr, IDC_ARROW),
          .lpszClassName = settingsClassName,
      }) {}

SettingsWindow::~SettingsWindow() {
    close();
}

Result<void> SettingsWindow::open(const settings::Settings& current, HICON icon) {
    setSettings(current);
    if (m_hwnd) {
        ShowWindow(m_hwnd.get(), SW_RESTORE);
        SetForegroundWindow(m_hwnd.get());
        return {};
    }
    if (!m_class.registered()) {
        return Error::fromLastError("RegisterClassEx(ThrenodySettings)");
    }

    // Fixed-size, centred on the work area, sized in DIPs for the primary
    // monitor's scale.
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    m_dpi = GetDpiForSystem();
    const int clientWidth = win32::scaleDip(config::settingsWindowWidthDip, m_dpi);
    const int clientHeight = win32::scaleDip(config::settingsWindowHeightDip, m_dpi);
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT frame{0, 0, clientWidth, clientHeight};
    AdjustWindowRectExForDpi(&frame, style, FALSE, 0, m_dpi);
    const int width = win32::width(frame);
    const int height = win32::height(frame);
    const int left = workArea.left + (win32::width(workArea) - width) / 2;
    const int top = workArea.top + (win32::height(workArea) - height) / 2;

    m_hwnd.reset(CreateWindowExW(0, m_class.name(), L"Threnody", style, left, top, width, height, nullptr, nullptr,
                                 m_instance, this));
    if (!m_hwnd) {
        return Error::fromLastError("CreateWindowEx(ThrenodySettings)");
    }
    m_dpi = GetDpiForWindow(m_hwnd.get());

    // Dark title bar and caption matching the ground colour (Windows 11).
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd.get(), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const COLORREF caption = RGB(10, 10, 10);
    DwmSetWindowAttribute(m_hwnd.get(), DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    const COLORREF captionText = RGB(237, 237, 237);
    DwmSetWindowAttribute(m_hwnd.get(), DWMWA_TEXT_COLOR, &captionText, sizeof(captionText));
    if (icon != nullptr) {
        SendMessageW(m_hwnd.get(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        SendMessageW(m_hwnd.get(), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    }

    if (const Result<void> device = createDevice(); !device) {
        m_hwnd.reset();
        return device;
    }
    setupImGui();

    ShowWindow(m_hwnd.get(), SW_SHOWNORMAL);
    SetForegroundWindow(m_hwnd.get());
    SetTimer(m_hwnd.get(), frameTimerId, frameMs, nullptr);
    return {};
}

void SettingsWindow::close() {
    if (!m_hwnd) {
        return;
    }
    KillTimer(m_hwnd.get(), frameTimerId);
    if (m_imgui != nullptr) {
        ImGui::SetCurrentContext(m_imgui);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(m_imgui);
        m_imgui = nullptr;
        m_bodyFont = m_headingFont = m_smallFont = nullptr;
    }
    destroyDevice();
    m_hwnd.reset();
}

void SettingsWindow::setSettings(const settings::Settings& current) {
    m_settings = current;
    const std::size_t length = std::min(current.spotifyClientId.size(), m_clientId.size() - 1);
    std::copy_n(current.spotifyClientId.data(), length, m_clientId.data());
    m_clientId[length] = '\0';
}

void SettingsWindow::setSpotifyStatus(SpotifyStatus status) {
    m_spotify = std::move(status);
}

Result<void> SettingsWindow::createDevice() {
    RECT client{};
    GetClientRect(m_hwnd.get(), &client);
    DXGI_SWAP_CHAIN_DESC swapChain{
        .BufferDesc = {.Width = static_cast<UINT>(win32::width(client)),
                       .Height = static_cast<UINT>(win32::height(client)),
                       .RefreshRate = {60, 1},
                       .Format = DXGI_FORMAT_B8G8R8A8_UNORM},
        .SampleDesc = {1, 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .OutputWindow = m_hwnd.get(),
        .Windowed = TRUE,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    };
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                               D3D11_SDK_VERSION, &swapChain, m_swapChain.put(), m_device.put(),
                                               nullptr, m_context.put());
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                                           &swapChain, m_swapChain.put(), m_device.put(), nullptr, m_context.put());
    }
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "D3D11CreateDeviceAndSwapChain(settings)");
    }
    return createRenderTarget();
}

Result<void> SettingsWindow::createRenderTarget() {
    winrt::com_ptr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.put_void());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IDXGISwapChain::GetBuffer");
    }
    hr = m_device->CreateRenderTargetView(backBuffer.get(), nullptr, m_renderTarget.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateRenderTargetView(settings)");
    }
    return {};
}

void SettingsWindow::destroyDevice() noexcept {
    m_renderTarget = nullptr;
    m_swapChain = nullptr;
    m_context = nullptr;
    m_device = nullptr;
}

void SettingsWindow::setupImGui() {
    IMGUI_CHECKVERSION();
    m_imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // Nothing to remember between runs.
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    const float scale = static_cast<float>(m_dpi) / 96.0f;
    const std::filesystem::path variable = systemFont(L"SegUIVar.ttf");
    const std::filesystem::path classic = systemFont(L"segoeui.ttf");
    const std::string fontFile = text::toUtf8(std::filesystem::exists(variable) ? variable.wstring() : classic.wstring());
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x2000, 0x206F, 0x2190, 0x21FF, 0};
    m_bodyFont = io.Fonts->AddFontFromFileTTF(fontFile.c_str(), std::round(15.0f * scale), nullptr, ranges);
    m_headingFont = io.Fonts->AddFontFromFileTTF(fontFile.c_str(), std::round(22.0f * scale), nullptr, ranges);
    m_smallFont = io.Fonts->AddFontFromFileTTF(fontFile.c_str(), std::round(12.0f * scale), nullptr, ranges);
    if (m_bodyFont == nullptr) {
        m_bodyFont = io.Fonts->AddFontDefault();
        m_headingFont = m_smallFont = m_bodyFont;
    }

    ImGui_ImplWin32_Init(m_hwnd.get());
    ImGui_ImplDX11_Init(m_device.get(), m_context.get());
    applyStyle();
}

void SettingsWindow::applyStyle() const {
    ImGuiStyle& style = ImGui::GetStyle();
    const float scale = static_cast<float>(m_dpi) / 96.0f;
    style.WindowRounding = 0.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2{28.0f, 16.0f};
    style.FramePadding = ImVec2{12.0f, 7.0f};
    style.ItemSpacing = ImVec2{10.0f, 7.0f};
    style.ItemInnerSpacing = ImVec2{10.0f, 6.0f};
    style.ScrollbarSize = 8.0f;
    style.ScaleAllSizes(scale);

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = colorGround;
    c[ImGuiCol_ChildBg] = colorGround;
    c[ImGuiCol_PopupBg] = colorSurface;
    c[ImGuiCol_Border] = colorBorder;
    c[ImGuiCol_BorderShadow] = ImVec4{0, 0, 0, 0};
    c[ImGuiCol_Text] = colorText;
    c[ImGuiCol_TextDisabled] = colorDisabled;
    c[ImGuiCol_FrameBg] = colorGround;
    c[ImGuiCol_FrameBgHovered] = colorSurfaceHover;
    c[ImGuiCol_FrameBgActive] = colorSurfaceActive;
    c[ImGuiCol_CheckMark] = colorText;
    c[ImGuiCol_CheckboxSelectedBg] = colorSurfaceActive;
    c[ImGuiCol_InputTextCursor] = colorText;
    c[ImGuiCol_TextSelectedBg] = ImVec4{1, 1, 1, 0.20f};
    c[ImGuiCol_TextLink] = colorText;
    c[ImGuiCol_Button] = colorGround;
    c[ImGuiCol_ButtonHovered] = colorSurfaceHover;
    c[ImGuiCol_ButtonActive] = colorSurfaceActive;
    c[ImGuiCol_Separator] = colorBorder;
    c[ImGuiCol_SeparatorHovered] = colorBorder;
    c[ImGuiCol_SeparatorActive] = colorBorder;
    c[ImGuiCol_ScrollbarBg] = colorGround;
    c[ImGuiCol_ScrollbarGrab] = colorBorder;
    c[ImGuiCol_ScrollbarGrabHovered] = colorSurfaceActive;
    c[ImGuiCol_ScrollbarGrabActive] = colorSurfaceActive;
    c[ImGuiCol_NavCursor] = ImVec4{1, 1, 1, 0.25f};
    c[ImGuiCol_TitleBg] = colorGround;
    c[ImGuiCol_TitleBgActive] = colorGround;
}

void SettingsWindow::sectionLabel(const char* text) const {
    ImGui::PushFont(m_smallFont);
    ImGui::PushStyleColor(ImGuiCol_Text, colorMuted);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Spacing();
}

void SettingsWindow::changed() {
    if (m_actions.onChanged) {
        defer([this, snapshot = m_settings] { m_actions.onChanged(snapshot); });
    }
}

void SettingsWindow::defer(std::function<void()> action) {
    m_deferred = std::move(action);
}

void SettingsWindow::drawContents() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                       ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##settings", nullptr, flags);
    ImGui::PushFont(m_bodyFont);

    ImGui::PushFont(m_headingFont);
    ImGui::TextUnformatted("Threnody");
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, colorMuted);
    ImGui::TextUnformatted("Ajustes. Los cambios se aplican al momento.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2{0, 2});
    ImGui::Separator();
    ImGui::Dummy(ImVec2{0, 2});

    sectionLabel("GENERAL");
    if (ImGui::Checkbox("Arrancar con Windows", &m_settings.startWithWindows)) {
        changed();
    }
    ImGui::Dummy(ImVec2{0, 2});
    ImGui::Separator();
    ImGui::Dummy(ImVec2{0, 2});

    sectionLabel("TECLAS DE BLOQUEO");
    if (ImGui::Checkbox("Mostrar un aviso al pulsar una tecla de bloqueo", &m_settings.lockKeys.enabled)) {
        changed();
    }
    ImGui::BeginDisabled(!m_settings.lockKeys.enabled);
    ImGui::Indent();
    if (ImGui::Checkbox("Bloq Mayús", &m_settings.lockKeys.capsLock)) changed();
    ImGui::SameLine(0, 24);
    if (ImGui::Checkbox("Bloq Num", &m_settings.lockKeys.numLock)) changed();
    ImGui::SameLine(0, 24);
    if (ImGui::Checkbox("Bloq Despl", &m_settings.lockKeys.scrollLock)) changed();
    ImGui::SameLine(0, 24);
    if (ImGui::Checkbox("Insert", &m_settings.lockKeys.insert)) changed();
    if (secondaryButton("Probar el aviso") && m_actions.onTestOverlay) {
        defer(m_actions.onTestOverlay);
    }
    ImGui::Unindent();
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2{0, 2});
    ImGui::Separator();
    ImGui::Dummy(ImVec2{0, 2});

    sectionLabel("VISUALIZADOR");
    int mode = m_settings.colorMode == ColorMode::Rainbow ? 1 : 0;
    if (ImGui::RadioButton("Color de la canción", &mode, 0)) {
        m_settings.colorMode = ColorMode::Track;
        changed();
    }
    ImGui::SameLine(0, 24);
    if (ImGui::RadioButton("Arcoíris", &mode, 1)) {
        m_settings.colorMode = ColorMode::Rainbow;
        changed();
    }
    ImGui::Dummy(ImVec2{0, 2});
    ImGui::Separator();
    ImGui::Dummy(ImVec2{0, 2});

    sectionLabel("SPOTIFY");
    ImGui::PushStyleColor(ImGuiCol_Text, colorMuted);
    ImGui::TextWrapped("Sin conexión, el título y el artista abren una búsqueda en Spotify. Conectado, abren la "
                       "canción y el artista exactos.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::TextUnformatted(m_spotify.connected ? "Estado: conectado" : "Estado: no conectado");
    if (!m_spotify.detail.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, colorMuted);
        ImGui::TextWrapped("%s", m_spotify.detail.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    if (m_spotify.connected) {
        if (secondaryButton("Desconectar") && m_actions.onDisconnectSpotify) {
            defer(m_actions.onDisconnectSpotify);
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, colorMuted);
        ImGui::TextWrapped("1. Crea una app en el panel de desarrolladores de Spotify.");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (secondaryButton("Abrir el panel")) {
            defer([] {
                ShellExecuteW(nullptr, L"open", L"https://developer.spotify.com/dashboard", nullptr, nullptr,
                              SW_SHOWNORMAL);
            });
        }
        ImGui::PushStyleColor(ImGuiCol_Text, colorMuted);
        ImGui::TextWrapped("2. Añádele esta URI de redirección:");
        ImGui::PopStyleColor();
        static std::string redirect = text::toUtf8(config::spotifyRedirectUri);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##redirect", redirect.data(), redirect.size() + 1, ImGuiInputTextFlags_ReadOnly);
        ImGui::PushStyleColor(ImGuiCol_Text, colorMuted);
        ImGui::TextWrapped("3. Pega aquí su Client ID y conecta. Se abrirá el navegador para autorizar.");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Conectar").x -
                                ImGui::GetStyle().FramePadding.x * 2 - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputTextWithHint("##clientid", "Client ID", m_clientId.data(), m_clientId.size(),
                                 ImGuiInputTextFlags_CharsNoBlank);
        ImGui::SameLine();
        const bool hasId = m_clientId[0] != '\0';
        ImGui::BeginDisabled(!hasId);
        if (primaryButton("Conectar") && m_actions.onConnectSpotify) {
            defer([this, clientId = std::string{m_clientId.data()}] { m_actions.onConnectSpotify(clientId); });
        }
        ImGui::EndDisabled();
    }

    // Footer pinned to the bottom.
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
    const float remaining = ImGui::GetContentRegionAvail().y - footerHeight;
    if (remaining > 0.0f) {
        ImGui::Dummy(ImVec2{0, remaining});
    }
    ImGui::Separator();
    if (secondaryButton("Salir de Threnody") && m_actions.onQuit) {
        defer(m_actions.onQuit);
    }
    ImGui::SameLine();
    ImGui::PushFont(m_smallFont);
    ImGui::PushStyleColor(ImGuiCol_Text, colorDisabled);
    const char* version = "Threnody, compilación de desarrollo";
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(version).x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) / 2.0f);
    ImGui::TextUnformatted(version);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::PopFont();
    ImGui::End();
}

void SettingsWindow::render() {
    if (!m_imgui || !m_renderTarget || m_rendering) {
        return;
    }
    m_rendering = true;
    ImGui::SetCurrentContext(m_imgui);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    drawContents();
    ImGui::Render();

    ID3D11RenderTargetView* target = m_renderTarget.get();
    m_context->OMSetRenderTargets(1, &target, nullptr);
    const float clear[4] = {colorGround.x, colorGround.y, colorGround.z, 1.0f};
    m_context->ClearRenderTargetView(target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swapChain->Present(1, 0);
    m_rendering = false;

    // Outside the frame now; the action may close this window or pump messages.
    if (m_deferred) {
        std::function<void()> action = std::move(m_deferred);
        m_deferred = nullptr;
        action();
    }
}

LRESULT CALLBACK SettingsWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->handle(hwnd, message, wParam, lParam);
}

LRESULT SettingsWindow::handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (m_imgui != nullptr) {
        ImGui::SetCurrentContext(m_imgui);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam) != 0) {
            return 1;
        }
    }
    switch (message) {
        case WM_TIMER:
            if (wParam == frameTimerId) {
                render();
            }
            return 0;
        case WM_SIZE:
            if (m_swapChain && wParam != SIZE_MINIMIZED) {
                m_renderTarget = nullptr;
                m_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                if (const Result<void> target = createRenderTarget(); !target) {
                    log::error("{}", target.error().describe());
                }
            }
            return 0;
        case WM_CLOSE:
            close();
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (m_hwnd.get() == hwnd) {
                m_hwnd.release();
            }
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace threnody::tray
