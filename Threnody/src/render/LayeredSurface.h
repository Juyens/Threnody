#pragma once

#include "util/Result.h"
#include "util/Win32.h"

#include <Windows.h>

#include <cstdint>
#include <span>

namespace threnody::render {

// Off-screen 32-bit BGRA premultiplied bitmap that is pushed to a per-pixel
// layered window with UpdateLayeredWindow. Owns the memory DC and the DIB
// section; `pixels()` and `dc()` expose them for whoever draws.
class LayeredSurface {
public:
    LayeredSurface() = default;
    ~LayeredSurface();

    LayeredSurface(const LayeredSurface&) = delete;
    LayeredSurface& operator=(const LayeredSurface&) = delete;

    // Recreates the bitmap when the size changes; keeps it otherwise.
    [[nodiscard]] Result<void> resize(SIZE size);

    [[nodiscard]] SIZE size() const noexcept { return m_size; }
    [[nodiscard]] HDC dc() const noexcept { return m_dc.get(); }
    [[nodiscard]] std::span<std::uint32_t> pixels() noexcept;

    // Hands the bitmap to DWM as the window's content. The window keeps its
    // position; its size follows the bitmap. `constantAlpha` multiplies the
    // whole surface (255 = as drawn), which is how fades are done cheaply.
    [[nodiscard]] Result<void> present(HWND hwnd, BYTE constantAlpha = 255) const;

private:
    void release() noexcept;

    win32::unique_hdc m_dc;
    win32::unique_hbitmap m_bitmap;
    HGDIOBJ m_previousBitmap{};
    std::uint32_t* m_bits{};
    SIZE m_size{};
};

}  // namespace threnody::render
