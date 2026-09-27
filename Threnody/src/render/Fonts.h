#pragma once

#include "util/Result.h"

#include <unknwn.h>
#include <dwrite_2.h>
#include <winrt/base.h>

namespace threnody::render {

// The text formats the widget uses (title and artist, in the bar's sizes and
// the card's), sharing one font fallback chain that routes CJK ranges to the
// configured families before the system fallback. All are single-line with a
// trailing ellipsis.
class Fonts {
public:
    [[nodiscard]] static Result<Fonts> create(IDWriteFactory2& factory);

    [[nodiscard]] IDWriteTextFormat& title() const noexcept { return *m_title; }
    [[nodiscard]] IDWriteTextFormat& artist() const noexcept { return *m_artist; }
    [[nodiscard]] IDWriteTextFormat& cardTitle() const noexcept { return *m_cardTitle; }
    [[nodiscard]] IDWriteTextFormat& cardArtist() const noexcept { return *m_cardArtist; }

private:
    winrt::com_ptr<IDWriteFontFallback> m_fallback;
    winrt::com_ptr<IDWriteTextFormat> m_title;
    winrt::com_ptr<IDWriteTextFormat> m_artist;
    winrt::com_ptr<IDWriteTextFormat> m_cardTitle;
    winrt::com_ptr<IDWriteTextFormat> m_cardArtist;
};

}  // namespace threnody::render
