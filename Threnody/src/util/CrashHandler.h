#pragma once

// Installs a process-wide unhandled-exception filter that writes the crash
// cause to the log before the process dies, so a crash exit is
// distinguishable from a voluntary one.
namespace threnody::crash {

void install() noexcept;

}  // namespace threnody::crash
