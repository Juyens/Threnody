#include "render/LayeredSurface.h"

namespace threnody::render {

LayeredSurface::~LayeredSurface() {
    release();
}

void LayeredSurface::release() noexcept {
    if (m_dc && m_previousBitmap != nullptr) {
        SelectObject(m_dc.get(), m_previousBitmap);
        m_previousBitmap = nullptr;
    }
    m_bitmap.reset();
    m_dc.reset();
    m_bits = nullptr;
    m_size = {};
}

Result<void> LayeredSurface::resize(SIZE size) {
    if (size.cx <= 0 || size.cy <= 0) {
        return Error::fromHResult(E_INVALIDARG, "LayeredSurface::resize with empty size");
    }
    if (m_bitmap && size.cx == m_size.cx && size.cy == m_size.cy) {
        return {};
    }
    release();

    m_dc.reset(CreateCompatibleDC(nullptr));
    if (!m_dc) {
        return Error::fromLastError("CreateCompatibleDC");
    }

    // Top-down so that row 0 is the top of the window, matching Direct2D and
    // every other rasteriser that will draw into it.
    const BITMAPINFO info{
        .bmiHeader =
            {
                .biSize = sizeof(BITMAPINFOHEADER),
                .biWidth = size.cx,
                .biHeight = -size.cy,
                .biPlanes = 1,
                .biBitCount = 32,
                .biCompression = BI_RGB,
            },
    };
    void* bits = nullptr;
    m_bitmap.reset(CreateDIBSection(m_dc.get(), &info, DIB_RGB_COLORS, &bits, nullptr, 0));
    if (!m_bitmap || bits == nullptr) {
        const Error error = Error::fromLastError("CreateDIBSection");
        release();
        return error;
    }

    m_previousBitmap = SelectObject(m_dc.get(), m_bitmap.get());
    m_bits = static_cast<std::uint32_t*>(bits);
    m_size = size;
    return {};
}

std::span<std::uint32_t> LayeredSurface::pixels() noexcept {
    if (m_bits == nullptr) {
        return {};
    }
    return {m_bits, static_cast<std::size_t>(m_size.cx) * static_cast<std::size_t>(m_size.cy)};
}

Result<void> LayeredSurface::present(HWND hwnd, BYTE constantAlpha) const {
    if (!m_dc || hwnd == nullptr) {
        return Error::fromHResult(E_NOT_VALID_STATE, "LayeredSurface::present without a surface or window");
    }
    POINT origin{};
    SIZE size = m_size;
    BLENDFUNCTION blend{
        .BlendOp = AC_SRC_OVER,
        .BlendFlags = 0,
        .SourceConstantAlpha = constantAlpha,
        .AlphaFormat = AC_SRC_ALPHA,
    };
    if (!UpdateLayeredWindow(hwnd, nullptr, nullptr, &size, m_dc.get(), &origin, 0, &blend, ULW_ALPHA)) {
        return Error::fromLastError("UpdateLayeredWindow");
    }
    return {};
}

}  // namespace threnody::render
