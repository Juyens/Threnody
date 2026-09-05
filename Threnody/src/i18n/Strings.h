#pragma once

#include <string_view>

// The two interface languages. Every user-facing string lives here, in both
// forms the code needs: UTF-16 for Win32/DirectWrite and UTF-8 for Dear ImGui.
namespace threnody::i18n {

enum class Language { Spanish, English };

struct Text {
    const wchar_t* wide;
    const char* utf8;
};

struct Strings {
    // Widget placeholder while Spotify has no session.
    Text placeholderTitle;
    Text placeholderArtist;

    // Tray menu.
    Text menuSettings;
    Text menuQuit;

    // Lock-key overlay.
    Text capsLock;
    Text numLock;
    Text scrollLock;
    Text insertPressed;
    Text lockOn;
    Text lockOff;

    // Settings window.
    Text settingsSubtitle;
    Text sectionGeneral;
    Text startWithWindows;
    Text language;
    Text sectionLockKeys;
    Text lockKeysEnabled;
    Text testOverlay;
    Text sectionVisualiser;
    Text modeTrack;
    Text modeRainbow;
    Text modeGradient;
    Text sectionSpotify;
    Text spotifyExplanation;
    Text spotifyConnected;
    Text spotifyDisconnected;
    Text disconnect;
    Text spotifyStep1;
    Text openDashboard;
    Text spotifyStep2;
    Text spotifyStep3;
    Text clientIdHint;
    Text connect;
    Text quit;
    Text showLog;
    Text hideLog;
    Text sectionLog;
    Text filter;
    Text follow;
    Text openFile;
    Text linesSuffix;  // Appended after the line count.

    // Spotify authorisation progress, composed with a technical detail.
    Text authWaitingForBrowser;
    Text authExchanging;
    Text authFailedPrefix;
};

inline constexpr Strings spanish{
    .placeholderTitle = {L"Spotify", "Spotify"},
    .placeholderArtist = {L"Nada en reproducción", "Nada en reproducción"},
    .menuSettings = {L"Ajustes…", "Ajustes…"},
    .menuQuit = {L"Salir", "Salir"},
    .capsLock = {L"Bloq Mayús", "Bloq Mayús"},
    .numLock = {L"Bloq Num", "Bloq Num"},
    .scrollLock = {L"Bloq Despl", "Bloq Despl"},
    .insertPressed = {L"Insert pulsado", "Insert pulsado"},
    .lockOn = {L"activado", "activado"},
    .lockOff = {L"desactivado", "desactivado"},
    .settingsSubtitle = {L"Ajustes. Los cambios se aplican al momento.", "Ajustes. Los cambios se aplican al momento."},
    .sectionGeneral = {L"GENERAL", "GENERAL"},
    .startWithWindows = {L"Arrancar con Windows", "Arrancar con Windows"},
    .language = {L"Idioma", "Idioma"},
    .sectionLockKeys = {L"TECLAS DE BLOQUEO", "TECLAS DE BLOQUEO"},
    .lockKeysEnabled = {L"Mostrar un aviso al pulsar una tecla de bloqueo", "Mostrar un aviso al pulsar una tecla de bloqueo"},
    .testOverlay = {L"Probar el aviso", "Probar el aviso"},
    .sectionVisualiser = {L"VISUALIZADOR", "VISUALIZADOR"},
    .modeTrack = {L"Color de la canción", "Color de la canción"},
    .modeRainbow = {L"Arcoíris", "Arcoíris"},
    .modeGradient = {L"Degradado de la canción", "Degradado de la canción"},
    .sectionSpotify = {L"SPOTIFY", "SPOTIFY"},
    .spotifyExplanation = {L"", "Sin conexión, el título y el artista abren una búsqueda en Spotify. Conectado, abren la canción y el artista exactos."},
    .spotifyConnected = {L"Estado: conectado", "Estado: conectado"},
    .spotifyDisconnected = {L"Estado: no conectado", "Estado: no conectado"},
    .disconnect = {L"Desconectar", "Desconectar"},
    .spotifyStep1 = {L"", "1. Crea una app en el panel de desarrolladores de Spotify."},
    .openDashboard = {L"Abrir el panel", "Abrir el panel"},
    .spotifyStep2 = {L"", "2. Añádele esta URI de redirección:"},
    .spotifyStep3 = {L"", "3. Pega aquí su Client ID y conecta. Se abrirá el navegador para autorizar."},
    .clientIdHint = {L"Client ID", "Client ID"},
    .connect = {L"Conectar", "Conectar"},
    .quit = {L"Salir de Threnody", "Salir de Threnody"},
    .showLog = {L"Ver registro", "Ver registro"},
    .hideLog = {L"Ocultar registro", "Ocultar registro"},
    .sectionLog = {L"REGISTRO EN VIVO", "REGISTRO EN VIVO"},
    .filter = {L"Filtrar", "Filtrar"},
    .follow = {L"Seguir", "Seguir"},
    .openFile = {L"Abrir archivo", "Abrir archivo"},
    .linesSuffix = {L"líneas", "líneas"},
    .authWaitingForBrowser = {L"", "Autoriza Threnody en el navegador que se acaba de abrir."},
    .authExchanging = {L"", "Intercambiando el código de autorización…"},
    .authFailedPrefix = {L"", "No se pudo conectar: "},
};

inline constexpr Strings english{
    .placeholderTitle = {L"Spotify", "Spotify"},
    .placeholderArtist = {L"Nothing playing", "Nothing playing"},
    .menuSettings = {L"Settings…", "Settings…"},
    .menuQuit = {L"Quit", "Quit"},
    .capsLock = {L"Caps Lock", "Caps Lock"},
    .numLock = {L"Num Lock", "Num Lock"},
    .scrollLock = {L"Scroll Lock", "Scroll Lock"},
    .insertPressed = {L"Insert pressed", "Insert pressed"},
    .lockOn = {L"on", "on"},
    .lockOff = {L"off", "off"},
    .settingsSubtitle = {L"Settings. Changes apply immediately.", "Settings. Changes apply immediately."},
    .sectionGeneral = {L"GENERAL", "GENERAL"},
    .startWithWindows = {L"Start with Windows", "Start with Windows"},
    .language = {L"Language", "Language"},
    .sectionLockKeys = {L"LOCK KEYS", "LOCK KEYS"},
    .lockKeysEnabled = {L"Show a notice when a lock key is pressed", "Show a notice when a lock key is pressed"},
    .testOverlay = {L"Test the notice", "Test the notice"},
    .sectionVisualiser = {L"VISUALISER", "VISUALISER"},
    .modeTrack = {L"Track colour", "Track colour"},
    .modeRainbow = {L"Rainbow", "Rainbow"},
    .modeGradient = {L"Track gradient", "Track gradient"},
    .sectionSpotify = {L"SPOTIFY", "SPOTIFY"},
    .spotifyExplanation = {L"", "Without a connection, the title and artist open a Spotify search. Connected, they open the exact track and artist."},
    .spotifyConnected = {L"Status: connected", "Status: connected"},
    .spotifyDisconnected = {L"Status: not connected", "Status: not connected"},
    .disconnect = {L"Disconnect", "Disconnect"},
    .spotifyStep1 = {L"", "1. Create an app in the Spotify developer dashboard."},
    .openDashboard = {L"Open dashboard", "Open dashboard"},
    .spotifyStep2 = {L"", "2. Add this redirect URI to it:"},
    .spotifyStep3 = {L"", "3. Paste its Client ID here and connect. The browser opens to authorise."},
    .clientIdHint = {L"Client ID", "Client ID"},
    .connect = {L"Connect", "Connect"},
    .quit = {L"Quit Threnody", "Quit Threnody"},
    .showLog = {L"Show log", "Show log"},
    .hideLog = {L"Hide log", "Hide log"},
    .sectionLog = {L"LIVE LOG", "LIVE LOG"},
    .filter = {L"Filter", "Filter"},
    .follow = {L"Follow", "Follow"},
    .openFile = {L"Open file", "Open file"},
    .linesSuffix = {L"lines", "lines"},
    .authWaitingForBrowser = {L"", "Authorise Threnody in the browser that just opened."},
    .authExchanging = {L"", "Exchanging the authorisation code…"},
    .authFailedPrefix = {L"", "Could not connect: "},
};

[[nodiscard]] constexpr const Strings& strings(Language language) noexcept {
    return language == Language::English ? english : spanish;
}

[[nodiscard]] constexpr std::string_view languageCode(Language language) noexcept {
    return language == Language::English ? "en" : "es";
}

[[nodiscard]] constexpr Language languageFromCode(std::string_view code) noexcept {
    return code == "en" ? Language::English : Language::Spanish;
}

}  // namespace threnody::i18n
