#include "render/CoverSampler.h"

#include <winrt/base.h>

namespace threnody::render {

Result<std::vector<std::uint32_t>> sampleCover(IWICImagingFactory& wic, std::span<const std::uint8_t> encoded,
                                               UINT size) {
    if (encoded.empty() || size == 0) {
        return Error::fromHResult(E_INVALIDARG, "sampleCover: empty input");
    }

    winrt::com_ptr<IWICStream> stream;
    HRESULT hr = wic.CreateStream(stream.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IWICImagingFactory::CreateStream");
    }
    hr = stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()), static_cast<DWORD>(encoded.size()));
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IWICStream::InitializeFromMemory");
    }

    winrt::com_ptr<IWICBitmapDecoder> decoder;
    hr = wic.CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateDecoderFromStream(cover sample)");
    }
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IWICBitmapDecoder::GetFrame");
    }

    winrt::com_ptr<IWICBitmapScaler> scaler;
    hr = wic.CreateBitmapScaler(scaler.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateBitmapScaler");
    }
    hr = scaler->Initialize(frame.get(), size, size, WICBitmapInterpolationModeFant);
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IWICBitmapScaler::Initialize(sample)");
    }

    winrt::com_ptr<IWICFormatConverter> converter;
    hr = wic.CreateFormatConverter(converter.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreateFormatConverter");
    }
    hr = converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IWICFormatConverter::Initialize(BGRA)");
    }

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(size) * size);
    const UINT stride = size * sizeof(std::uint32_t);
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size() * sizeof(std::uint32_t)),
                               reinterpret_cast<BYTE*>(pixels.data()));
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "IWICBitmapSource::CopyPixels");
    }
    return pixels;
}

}  // namespace threnody::render
