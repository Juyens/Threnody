#include "render/Fonts.h"

#include "Config.h"

#include <array>
#include <span>

namespace threnody::render {
namespace {

struct FallbackRule {
    std::span<const DWRITE_UNICODE_RANGE> ranges;
    const wchar_t* family;
};

constexpr std::array japaneseRanges{
    DWRITE_UNICODE_RANGE{0x3000, 0x303F},  // CJK symbols and punctuation
    DWRITE_UNICODE_RANGE{0x3040, 0x30FF},  // Hiragana, Katakana
    DWRITE_UNICODE_RANGE{0x31F0, 0x31FF},  // Katakana phonetic extensions
    DWRITE_UNICODE_RANGE{0x3400, 0x4DBF},  // CJK Unified Ideographs Extension A
    DWRITE_UNICODE_RANGE{0x4E00, 0x9FFF},  // CJK Unified Ideographs
    DWRITE_UNICODE_RANGE{0xFF00, 0xFFEF},  // Halfwidth and fullwidth forms
};

constexpr std::array chineseRanges{
    DWRITE_UNICODE_RANGE{0x3100, 0x312F},  // Bopomofo
    DWRITE_UNICODE_RANGE{0x20000, 0x2A6DF},  // CJK Unified Ideographs Extension B
};

constexpr std::array koreanRanges{
    DWRITE_UNICODE_RANGE{0x1100, 0x11FF},  // Hangul Jamo
    DWRITE_UNICODE_RANGE{0x3130, 0x318F},  // Hangul compatibility Jamo
    DWRITE_UNICODE_RANGE{0xAC00, 0xD7AF},  // Hangul syllables
};

// Unified Han is shared by Japanese and Chinese; Japanese wins here because
// that is what this user listens to. Chinese gets the ranges Japanese fonts
// never carry.
Result<winrt::com_ptr<IDWriteFontFallback>> createFallback(IDWriteFactory2& factory) {
    winrt::com_ptr<IDWriteFontFallbackBuilder> builder;
    HRESULT hr = factory.CreateFontFallbackBuilder(builder.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateFontFallbackBuilder");
    }

    const FallbackRule rules[] = {
        {japaneseRanges, config::fontFamilyJapanese},
        {chineseRanges, config::fontFamilyChinese},
        {koreanRanges, config::fontFamilyKorean},
    };
    for (const FallbackRule& rule : rules) {
        const wchar_t* families[] = {rule.family};
        hr = builder->AddMapping(rule.ranges.data(), static_cast<UINT32>(rule.ranges.size()), families, 1);
        if (FAILED(hr)) {
            return Error::fromHResult(hr, "IDWriteFontFallbackBuilder::AddMapping");
        }
    }

    winrt::com_ptr<IDWriteFontFallback> systemFallback;
    hr = factory.GetSystemFontFallback(systemFallback.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "GetSystemFontFallback");
    }
    hr = builder->AddMappings(systemFallback.get());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IDWriteFontFallbackBuilder::AddMappings(system)");
    }

    winrt::com_ptr<IDWriteFontFallback> fallback;
    hr = builder->CreateFontFallback(fallback.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateFontFallback");
    }
    return fallback;
}

Result<winrt::com_ptr<IDWriteTextFormat>> createFormat(IDWriteFactory2& factory, IDWriteFontFallback& fallback,
                                                       float sizeDip, DWRITE_FONT_WEIGHT weight,
                                                       const char* what) {
    winrt::com_ptr<IDWriteTextFormat> format;
    HRESULT hr = factory.CreateTextFormat(config::fontFamily, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, sizeDip, L"en-US", format.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, std::string("CreateTextFormat ") + what);
    }

    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    winrt::com_ptr<IDWriteInlineObject> ellipsis;
    hr = factory.CreateEllipsisTrimmingSign(format.get(), ellipsis.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateEllipsisTrimmingSign");
    }
    const DWRITE_TRIMMING trimming{.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER};
    format->SetTrimming(&trimming, ellipsis.get());

    if (const auto format1 = format.try_as<IDWriteTextFormat1>()) {
        format1->SetFontFallback(&fallback);
    }
    return format;
}

}  // namespace

Result<Fonts> Fonts::create(IDWriteFactory2& factory) {
    Fonts fonts;

    Result<winrt::com_ptr<IDWriteFontFallback>> fallback = createFallback(factory);
    if (!fallback) {
        return fallback.error();
    }
    fonts.m_fallback = std::move(fallback.value());

    Result<winrt::com_ptr<IDWriteTextFormat>> title =
        createFormat(factory, *fonts.m_fallback, config::titleFontSizeDip, DWRITE_FONT_WEIGHT_SEMI_BOLD, "title");
    if (!title) {
        return title.error();
    }
    fonts.m_title = std::move(title.value());

    Result<winrt::com_ptr<IDWriteTextFormat>> artist =
        createFormat(factory, *fonts.m_fallback, config::artistFontSizeDip, DWRITE_FONT_WEIGHT_NORMAL, "artist");
    if (!artist) {
        return artist.error();
    }
    fonts.m_artist = std::move(artist.value());

    return fonts;
}

}  // namespace threnody::render
