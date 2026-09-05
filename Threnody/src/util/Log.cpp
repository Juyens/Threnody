#include "util/Log.h"

#include "Config.h"

#include <chrono>
#include <fstream>
#include <mutex>
#include <system_error>

namespace threnody::log {
namespace {

struct State {
    std::mutex mutex;
    std::ofstream file;
};

State& state() {
    static State instance;
    return instance;
}

constexpr std::string_view levelName(Level level) noexcept {
    switch (level) {
        case Level::Info: return "INFO ";
        case Level::Warn: return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?????";
}

std::string timestamp() {
    using namespace std::chrono;
    const auto now = floor<milliseconds>(system_clock::now());
    try {
        return std::format("{:%F %T}", zoned_time{current_zone(), now});
    } catch (const std::exception&) {
        return std::format("{:%F %T}Z", now);
    }
}

// Keep the previous run's tail as `.1` when the file grows past the cap.
void rotateIfLarge(const std::filesystem::path& file) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size < config::logMaxBytes) {
        return;
    }
    auto previous = file;
    previous += L".1";
    std::filesystem::rename(file, previous, ec);
}

}  // namespace

void init(const std::filesystem::path& file) {
    State& s = state();
    std::scoped_lock lock{s.mutex};
    rotateIfLarge(file);
    s.file.open(file, std::ios::app | std::ios::binary);
}

void write(Level level, std::string_view message) {
    State& s = state();
    std::scoped_lock lock{s.mutex};
    if (!s.file.is_open()) {
        return;
    }
    // Flushed per line: the log is sparse, and a crash must not lose the
    // lines that explain it.
    s.file << timestamp() << ' ' << levelName(level) << ' ' << message << std::endl;
}

void flush() {
    State& s = state();
    std::scoped_lock lock{s.mutex};
    s.file.flush();
}

void shutdown() {
    State& s = state();
    std::scoped_lock lock{s.mutex};
    s.file.flush();
    s.file.close();
}

}  // namespace threnody::log
