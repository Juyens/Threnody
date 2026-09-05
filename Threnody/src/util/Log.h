#pragma once

#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

// Append-only text log in the data directory. Thread-safe, but it takes a lock
// and touches the file system, so it must never be called from the audio thread.
namespace threnody::log {

enum class Level { Info, Warn, Error };

void init(const std::filesystem::path& file);
void write(Level level, std::string_view message);
void flush();
void shutdown();

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
