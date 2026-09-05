#include "Threnody.h"

#include "Config.h"
#include "app/Application.h"
#include "util/CrashHandler.h"
#include "util/Log.h"
#include "util/Paths.h"
#include "util/Win32.h"

#include <exception>

using namespace threnody;

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    // Single instance: a second copy would embed a second widget in the taskbar.
    win32::unique_handle instanceMutex{CreateMutexW(nullptr, TRUE, config::singleInstanceMutexName)};
    if (!instanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        return EXIT_SUCCESS;
    }

    if (const Result<std::filesystem::path> dataDir = paths::dataDirectory(); dataDir) {
        log::init(*dataDir / config::logFileName);
    }
    crash::install();
    log::info("Threnody starting, pid {}", GetCurrentProcessId());

    int exitCode = EXIT_FAILURE;
    try {
        Application app{instance};
        exitCode = app.run();
    } catch (const std::exception& e) {
        log::error("exit: unhandled std::exception: {}", e.what());
        log::shutdown();
        return EXIT_FAILURE;
    }

    log::info("exit: requested, code {}", exitCode);
    log::shutdown();
    return exitCode;
}
