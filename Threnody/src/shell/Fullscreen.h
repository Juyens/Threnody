#pragma once

namespace threnody::shell {

// True when the foreground window covers its whole monitor and is not the
// desktop or the shell: a game, a full-screen video, a presentation. Used to
// keep pop-ups out of the way.
[[nodiscard]] bool isFullscreenApplicationInFront() noexcept;

}  // namespace threnody::shell
