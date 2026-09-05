#include "util/CrashHandler.h"

#include "util/Log.h"

#include <Windows.h>
#include <Psapi.h>

#include <array>
#include <crtdbg.h>
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

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* pointers) noexcept {
    const EXCEPTION_RECORD* record = pointers != nullptr ? pointers->ExceptionRecord : nullptr;
    if (record != nullptr) {
        log::error("exit: crash, exception 0x{:08X} at {}", static_cast<unsigned long>(record->ExceptionCode),
                   describeAddress(record->ExceptionAddress));
    } else {
        log::error("exit: crash, no exception record");
    }
    log::shutdown();
    return EXCEPTION_EXECUTE_HANDLER;
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
#ifdef _DEBUG
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, reportHook);
#endif
}

}  // namespace threnody::crash
