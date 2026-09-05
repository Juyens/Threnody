#include "util/CrashHandler.h"

#include "util/Log.h"

#include <Windows.h>

namespace threnody::crash {
namespace {

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* pointers) noexcept {
    const EXCEPTION_RECORD* record = pointers != nullptr ? pointers->ExceptionRecord : nullptr;
    if (record != nullptr) {
        log::error("exit: crash, exception 0x{:08X} at {}",
                   static_cast<unsigned long>(record->ExceptionCode), record->ExceptionAddress);
    } else {
        log::error("exit: crash, no exception record");
    }
    log::shutdown();
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void install() noexcept {
    SetUnhandledExceptionFilter(onUnhandledException);
}

}  // namespace threnody::crash
