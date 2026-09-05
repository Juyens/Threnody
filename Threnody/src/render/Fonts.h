#pragma once

#include "util/Result.h"

#include <unknwn.h>
#include <dwrite_2.h>
#include <winrt/base.h>

namespace threnody::render {

// The two text formats the widget uses, sharing one font fallback chain that
// routes CJK ranges to the configured families before the system fallback.
// Both formats are single-line with a trailing ellipsis.
class Fonts {
public:
    [[nodiscard]] static Result<Fonts> create(IDWriteFactory2& factory);

    [[nodiscard]] IDWriteTextFormat& title() const noexcept { return *m_title; }
    [[nodiscard]] IDWriteTextFormat& artist() const noexcept { return *m_artist; }

private:
    winrt::com_ptr<IDWriteFontFallback> m_fallback;
    winrt::com_ptr<IDWriteTextFormat> m_title;
    winrt::com_ptr<IDWriteTextFormat> m_artist;
};

}  // namespace threnody::render
