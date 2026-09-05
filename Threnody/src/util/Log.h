#pragma once

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Append-only text log in the data directory. Thread-safe, but it takes a lock
// and touches the file system, so it must never be called from the audio thread.
namespace threnody::log {

enum class Level { Info, Warn, Error };

void init(const std::filesystem::path& file);
void write(Level level, std::string_view message);
void flush();
void shutdown();

// The file being written, empty before init.
[[nodiscard]] std::filesystem::path file();

// Recent lines, kept in memory for the live viewer in the settings window.
// Copies them into `out` and updates `revision` only when something was
// written since `revision`; returns whether it did.
bool copyRecentIfChanged(std::uint64_t& revision, std::vector<std::string>& out);

template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace threnody::log
