#include "Threnody.h"

#include "Config.h"
#include "app/Application.h"
#include "util/CrashHandler.h"
#include "util/Log.h"
#include "util/Paths.h"
#include "util/Win32.h"

#include <unknwn.h>
#include <winrt/base.h>

#include <exception>

using namespace threnody;

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    // Single instance: a second copy would embed a second widget in the taskbar.
    win32::unique_handle instanceMutex{CreateMutexW(nullptr, TRUE, config::singleInstanceMutexName)};
    if (!instanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        return EXIT_SUCCESS;
    }

    std::filesystem::path dataDirectory;
    if (const Result<std::filesystem::path> dir = paths::dataDirectory(); dir) {
        dataDirectory = *dir;
        log::init(dataDirectory / config::logFileName);
    }
    crash::install();
    log::info("Threnody starting, pid {}", GetCurrentProcessId());

    int exitCode = EXIT_FAILURE;
    try {
        // Single-threaded apartment on the UI thread: WIC, the tray icon and
        // the WinRT media session all live here.
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        Application app{instance, dataDirectory};
        exitCode = app.run();
    } catch (const winrt::hresult_error& e) {
        log::error("exit: unhandled hresult_error 0x{:08X}: {}", static_cast<unsigned long>(e.code().value),
                   winrt::to_string(e.message()));
        log::shutdown();
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        log::error("exit: unhandled std::exception: {}", e.what());
        log::shutdown();
        return EXIT_FAILURE;
    }

    log::info("exit: requested, code {}", exitCode);
    log::shutdown();
    return exitCode;
}
