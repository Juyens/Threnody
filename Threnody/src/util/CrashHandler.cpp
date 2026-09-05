#include "util/CrashHandler.h"

#include "util/Log.h"

#include <unknwn.h>
#include <winrt/base.h>

#include <Windows.h>
#include <DbgHelp.h>
#include <Psapi.h>

#include <array>
#include <crtdbg.h>
#include <cstdlib>
#include <exception>
#include <string>

namespace threnody::crash {
namespace {

// "module.dll+0x1234" for an address, so a crash can be placed without a
// debugger attached.
std::string describeAddress(const void* address) noexcept {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module) ||
        module == nullptr) {
        return std::format("{}", address);
    }
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    std::wstring_view name{path.data(), length};
    if (const std::size_t slash = name.find_last_of(L"\\/"); slash != std::wstring_view::npos) {
        name.remove_prefix(slash + 1);
    }
    const auto offset = reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module);
    std::string ascii;
    for (const wchar_t c : name) {
        ascii.push_back(c < 128 ? static_cast<char>(c) : '?');
    }
    return std::format("{}+0x{:X}", ascii, offset);
}

std::string narrow(const wchar_t* text) noexcept {
    std::string out;
    for (; text != nullptr && *text != L'\0'; ++text) {
        out.push_back(*text < 128 ? static_cast<char>(*text) : '?');
    }
    return out;
}

// Symbolised stack of the calling thread, from the debug information next to
// the executable. Crash path only: dbghelp is neither fast nor thread-safe.
void logStackTrace() noexcept {
    static bool initialised = false;
    if (!initialised) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        initialised = SymInitializeW(GetCurrentProcess(), nullptr, TRUE) != FALSE;
    }

    std::array<void*, 62> frames{};
    const USHORT count = CaptureStackBackTrace(1, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
    for (USHORT i = 0; i < count; ++i) {
        const auto address = reinterpret_cast<DWORD64>(frames[i]);
        std::string where = describeAddress(frames[i]);
        if (initialised) {
            alignas(SYMBOL_INFOW) std::array<char, sizeof(SYMBOL_INFOW) + 256 * sizeof(wchar_t)> buffer{};
            auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(buffer.data());
            symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
            symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            if (SymFromAddrW(GetCurrentProcess(), address, &displacement, symbol)) {
                where = std::format("{}+0x{:X}", narrow(symbol->Name), displacement);
                IMAGEHLP_LINEW64 line{.SizeOfStruct = sizeof(IMAGEHLP_LINEW64)};
                DWORD column = 0;
                if (SymGetLineFromAddrW64(GetCurrentProcess(), address, &column, &line)) {
                    std::wstring_view file{line.FileName};
                    if (const std::size_t slash = file.find_last_of(L"\\/"); slash != std::wstring_view::npos) {
                        file.remove_prefix(slash + 1);
                    }
                    where += std::format(" ({}:{})", narrow(std::wstring{file}.c_str()), line.LineNumber);
                }
            }
        }
        log::error("  #{:02} {}", i, where);
    }
}

void logCurrentException() noexcept {
    const std::exception_ptr current = std::current_exception();
    if (!current) {
        log::error("  no active exception");
        return;
    }
    try {
        std::rethrow_exception(current);
    } catch (const winrt::hresult_error& e) {
        log::error("  winrt::hresult_error 0x{:08X}: {}", static_cast<unsigned long>(e.code().value),
                   winrt::to_string(e.message()));
    } catch (const std::exception& e) {
        log::error("  std::exception: {}", e.what());
    } catch (...) {
        log::error("  unknown exception type");
    }
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* pointers) noexcept {
    const EXCEPTION_RECORD* record = pointers != nullptr ? pointers->ExceptionRecord : nullptr;
    if (record != nullptr) {
        log::error("exit: crash, exception 0x{:08X} at {}", static_cast<unsigned long>(record->ExceptionCode),
                   describeAddress(record->ExceptionAddress));
    } else {
        log::error("exit: crash, no exception record");
    }
    logStackTrace();
    log::shutdown();
    return EXCEPTION_EXECUTE_HANDLER;
}

// An exception escaped a thread body, a noexcept function or a coroutine:
// record what it was and where it came from before the runtime aborts.
[[noreturn]] void onTerminate() {
    log::error("exit: std::terminate on thread {}", GetCurrentThreadId());
    logCurrentException();
    logStackTrace();
    log::shutdown();
    std::abort();
}

#ifdef _DEBUG
// Debug CRT assertions (including Dear ImGui's IM_ASSERT) normally end in a
// dialog nobody reads; copy the text to the log first.
int reportHook(int reportType, char* message, int* returnValue) noexcept {
    if (message != nullptr) {
        log::error("CRT report type {}: {}", reportType, message);
    }
    if (returnValue != nullptr) {
        *returnValue = 0;
    }
    return FALSE;  // Let the default handler run as well.
}
#endif

}  // namespace

void install() noexcept {
    SetUnhandledExceptionFilter(onUnhandledException);
    std::set_terminate(onTerminate);
#ifdef _DEBUG
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, reportHook);
#endif
}

}  // namespace threnody::crash
