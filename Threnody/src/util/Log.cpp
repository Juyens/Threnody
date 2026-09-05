#include "util/Log.h"

#include "Config.h"

#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <system_error>

namespace threnody::log {
namespace {

struct State {
    std::mutex mutex;
    std::ofstream file;
    std::filesystem::path path;
    std::deque<std::string> recent;
    std::uint64_t revision{};
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
    s.path = file;
    s.file.open(file, std::ios::app | std::ios::binary);
}

std::filesystem::path file() {
    State& s = state();
    std::scoped_lock lock{s.mutex};
    return s.path;
}

bool copyRecentIfChanged(std::uint64_t& revision, std::vector<std::string>& out) {
    State& s = state();
    std::scoped_lock lock{s.mutex};
    if (revision == s.revision) {
        return false;
    }
    revision = s.revision;
    out.assign(s.recent.begin(), s.recent.end());
    return true;
}

void write(Level level, std::string_view message) {
    State& s = state();
    std::scoped_lock lock{s.mutex};
    std::string line = std::format("{} {} {}", timestamp(), levelName(level), message);
    s.recent.push_back(line);
    if (s.recent.size() > config::logRecentLines) {
        s.recent.pop_front();
    }
    ++s.revision;
    if (!s.file.is_open()) {
        return;
    }
    // Flushed per line: the log is sparse, and a crash must not lose the
    // lines that explain it.
    s.file << line << std::endl;
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
