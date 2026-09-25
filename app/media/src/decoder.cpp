#include "djivcam/decoder.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#if defined(_WIN32)
#include <d3d11.h>  // before the C block below: it declares C++ operators
#include <dxgi.h>
#include <wrl/client.h>
extern "C" {
#include <libavutil/hwcontext_d3d11va.h>
}
#endif

namespace djivcam::media {
namespace {

constexpr AVHWDeviceType kGpuBackends[] = {
#if defined(_WIN32)
    AV_HWDEVICE_TYPE_D3D11VA,
    AV_HWDEVICE_TYPE_DXVA2,
#elif defined(__APPLE__)
    AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#else
    AV_HWDEVICE_TYPE_VAAPI,
    AV_HWDEVICE_TYPE_CUDA,
    AV_HWDEVICE_TYPE_VDPAU,
#endif
};

// Pixel format the decoder produces for `type`, or AV_PIX_FMT_NONE if unsupported.
AVPixelFormat hw_pixel_format(const AVCodec* codec, AVHWDeviceType type) {
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config) {
            return AV_PIX_FMT_NONE;
        }
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == type) {
            return config->pix_fmt;
        }
    }
}

// The name of the graphics adapter a GPU device runs on, when the backend can tell, else empty.
std::string adapter_name([[maybe_unused]] const AVBufferRef* device, [[maybe_unused]] AVHWDeviceType type) {
#if defined(_WIN32)
    if (type == AV_HWDEVICE_TYPE_D3D11VA) {
        const auto* context = reinterpret_cast<const AVHWDeviceContext*>(device->data);
        ID3D11Device* d3d = static_cast<const AVD3D11VADeviceContext*>(context->hwctx)->device;
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC description{};
        if (FAILED(d3d->QueryInterface(IID_PPV_ARGS(dxgi.GetAddressOf()))) || FAILED(dxgi->GetAdapter(adapter.GetAddressOf())) ||
            FAILED(adapter->GetDesc(&description))) {
            return {};
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1) {
            return {};
        }
        std::string name(static_cast<std::size_t>(size - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name.data(), size, nullptr, nullptr);
        return name;
    }
#endif
    return {};
}

// Picks the GPU surface format when the decoder offers it (stored in context->opaque).
AVPixelFormat choose_format(AVCodecContext* context, const AVPixelFormat* offered) {
    const auto wanted = static_cast<AVPixelFormat>(reinterpret_cast<std::intptr_t>(context->opaque));
    for (const AVPixelFormat* format = offered; *format != AV_PIX_FMT_NONE; ++format) {
        if (*format == wanted) {
            return *format;
        }
    }
    return offered[0];  // GPU path refused for this stream: first software format
}

}  // namespace

struct H264Decoder::Impl {
    AVCodecContext* context = nullptr;
    AVBufferRef* device = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* downloaded = nullptr;  // GPU surface copied to system memory
    SwsContext* scaler = nullptr;
    AVPixelFormat hw_format = AV_PIX_FMT_NONE;
    std::string backend = "software";
    std::string gpu;
    std::vector<std::uint8_t> input;  // access unit + the zeroed padding FFmpeg requires

    ~Impl() { close(); }

    void close() {
        sws_freeContext(scaler);
        scaler = nullptr;
        av_frame_free(&downloaded);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&context);
        av_buffer_unref(&device);
    }

    // Opens the decoder, on the GPU `type` or on the CPU when type is NONE.
    bool open(const AVCodec* codec, AVHWDeviceType type) {
        close();
        context = avcodec_alloc_context3(codec);
        if (!context) {
            return false;
        }
        context->flags |= AV_CODEC_FLAG_LOW_DELAY;
        context->flags2 |= AV_CODEC_FLAG2_FAST;
        context->thread_count = 1;  // frame threading would hold back several frames
        if (type != AV_HWDEVICE_TYPE_NONE) {
            hw_format = hw_pixel_format(codec, type);
            if (hw_format == AV_PIX_FMT_NONE || av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0) {
                return false;
            }
            context->hw_device_ctx = av_buffer_ref(device);
            context->opaque = reinterpret_cast<void*>(static_cast<std::intptr_t>(hw_format));
            context->get_format = choose_format;
        }
        if (avcodec_open2(context, codec, nullptr) < 0) {
            return false;
        }
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        downloaded = av_frame_alloc();
        backend = type == AV_HWDEVICE_TYPE_NONE ? "software" : av_hwdevice_get_type_name(type);
        gpu = type == AV_HWDEVICE_TYPE_NONE ? std::string() : adapter_name(device, type);
        return packet && frame && downloaded;
    }

    // Packs `picture` into an Nv12Frame: a plain copy for GPU surfaces (already NV12), an unscaled
    // repack (no color or range conversion) for the software decoder's planar YUV. The color
    // description comes from `described`, the decoder's own frame (a GPU download lacks it).
    // NV12 needs even sizes: an odd last row or column is dropped. Empty on failure.
    std::optional<Nv12Frame> to_nv12(const AVFrame& picture, const AVFrame& described) {
        const int width = picture.width & ~1;
        const int height = picture.height & ~1;
        if (width < 2 || height < 2) {
            return std::nullopt;
        }
        Nv12Frame out;
        out.width = width;
        out.height = height;
        out.bt709 = described.colorspace == AVCOL_SPC_BT709 ||
                    (described.colorspace == AVCOL_SPC_UNSPECIFIED && height >= 720);  // HD default
        out.full_range = described.color_range == AVCOL_RANGE_JPEG || picture.format == AV_PIX_FMT_YUVJ420P;
        const std::size_t row = static_cast<std::size_t>(width);
        const std::size_t luma = row * static_cast<std::size_t>(height);
        out.data.resize(luma + luma / 2);
        std::uint8_t* planes[4] = {out.data.data(), out.data.data() + luma, nullptr, nullptr};
        if (picture.format == AV_PIX_FMT_NV12) {
            for (int y = 0; y < height; ++y) {
                std::memcpy(planes[0] + row * y, picture.data[0] + static_cast<std::ptrdiff_t>(picture.linesize[0]) * y, row);
            }
            for (int y = 0; y < height / 2; ++y) {
                std::memcpy(planes[1] + row * y, picture.data[1] + static_cast<std::ptrdiff_t>(picture.linesize[1]) * y, row);
            }
            return out;
        }
        scaler = sws_getCachedContext(scaler, width, height, static_cast<AVPixelFormat>(picture.format), width, height,
                                      AV_PIX_FMT_NV12, SWS_POINT, nullptr, nullptr, nullptr);
        if (!scaler) {
            return std::nullopt;
        }
        // Keep the source's range: full-range (JPEG) YUV stays full range, flagged in full_range.
        const int* coefficients = sws_getCoefficients(out.bt709 ? SWS_CS_ITU709 : SWS_CS_DEFAULT);
        const int range = out.full_range ? 1 : 0;
        sws_setColorspaceDetails(scaler, coefficients, range, coefficients, range, 0, 1 << 16, 1 << 16);
        const int strides[4] = {width, width, 0, 0};
        sws_scale(scaler, picture.data, picture.linesize, 0, height, planes, strides);
        return out;
    }
};

H264Decoder::H264Decoder(DecoderPreference preference) : impl_(std::make_unique<Impl>()) {
    av_log_set_level(AV_LOG_ERROR);
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        throw std::runtime_error("H.264 decoder unavailable");
    }
    bool opened = false;
    if (preference != DecoderPreference::Software) {
        for (AVHWDeviceType type : kGpuBackends) {
            if ((opened = impl_->open(codec, type))) {
                break;
            }
        }
        if (!opened && preference == DecoderPreference::Hardware) {
            throw std::runtime_error("no GPU H.264 decoder available");
        }
    }
    if (!opened && !impl_->open(codec, AV_HWDEVICE_TYPE_NONE)) {
        throw std::runtime_error("cannot open H.264 decoder");
    }
}

H264Decoder::~H264Decoder() = default;

const std::string& H264Decoder::backend() const { return impl_->backend; }

const std::string& H264Decoder::gpu() const { return impl_->gpu; }

bool H264Decoder::hardware() const { return impl_->device != nullptr; }

std::optional<Nv12Frame> H264Decoder::decode(std::span<const std::uint8_t> access_unit) {
    Impl& d = *impl_;
    d.input.assign(access_unit.begin(), access_unit.end());
    d.input.resize(access_unit.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    d.packet->data = d.input.data();
    d.packet->size = static_cast<int>(access_unit.size());
    if (avcodec_send_packet(d.context, d.packet) < 0) {
        return std::nullopt;
    }
    std::optional<Nv12Frame> latest;
    while (avcodec_receive_frame(d.context, d.frame) == 0) {
        const AVFrame* picture = d.frame;
        if (d.frame->format == d.hw_format && d.hw_format != AV_PIX_FMT_NONE) {
            // TODO: hand GPU surfaces to the renderer / virtual camera without this copy.
            if (av_hwframe_transfer_data(d.downloaded, d.frame, 0) < 0) {
                av_frame_unref(d.frame);
                continue;
            }
            picture = d.downloaded;
        }
        if (auto converted = d.to_nv12(*picture, *d.frame)) {
            latest = std::move(converted);
        }
        av_frame_unref(d.downloaded);
        av_frame_unref(d.frame);
    }
    return latest;
}

struct Nv12Canvas::Impl {
    int width;
    int height;
    std::vector<std::uint8_t> canvas;
    SwsContext* scaler = nullptr;
    int source_width = 0;
    int source_height = 0;

    ~Impl() { sws_freeContext(scaler); }

    void clear() {
        const std::size_t luma = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        std::fill(canvas.begin(), canvas.begin() + static_cast<std::ptrdiff_t>(luma), std::uint8_t{16});  // black
        std::fill(canvas.begin() + static_cast<std::ptrdiff_t>(luma), canvas.end(), std::uint8_t{128});   // neutral
    }
};

Nv12Canvas::Nv12Canvas(int width, int height) : impl_(std::make_unique<Impl>()) {
    impl_->width = width;
    impl_->height = height;
    impl_->canvas.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3 / 2);
    impl_->clear();
}

Nv12Canvas::~Nv12Canvas() = default;

const std::uint8_t* Nv12Canvas::draw(const Nv12Frame& frame) {
    Impl& d = *impl_;
    if (frame.width == d.width && frame.height == d.height) {
        return frame.data.data();
    }
    if (frame.width <= 0 || frame.height <= 0) {
        return d.canvas.data();
    }
    // Keep the aspect ratio, shrink only if needed; NV12 needs even sizes and offsets.
    const bool fits = frame.width <= d.width && frame.height <= d.height;
    const double scale = fits ? 1.0 : std::min(static_cast<double>(d.width) / frame.width, static_cast<double>(d.height) / frame.height);
    const int fitted_width = std::min(d.width, static_cast<int>(frame.width * scale) & ~1);
    const int fitted_height = std::min(d.height, static_cast<int>(frame.height * scale) & ~1);
    if (fitted_width < 2 || fitted_height < 2) {
        return d.canvas.data();  // degenerate frame (e.g. 4096x2): nothing sensible to show
    }
    const int left = ((d.width - fitted_width) / 2) & ~1;
    const int top = ((d.height - fitted_height) / 2) & ~1;
    if (frame.width != d.source_width || frame.height != d.source_height) {
        d.source_width = frame.width;
        d.source_height = frame.height;
        d.clear();  // new geometry: repaint the bars
    }
    const std::size_t canvas_width = static_cast<std::size_t>(d.width);
    std::uint8_t* luma = d.canvas.data() + static_cast<std::size_t>(top) * canvas_width + left;
    std::uint8_t* chroma = d.canvas.data() + canvas_width * d.height + static_cast<std::size_t>(top / 2) * canvas_width + left;
    const std::uint8_t* source_luma = frame.data.data();
    const std::uint8_t* source_chroma = source_luma + static_cast<std::size_t>(frame.width) * frame.height;
    if (fits) {
        for (int row = 0; row < fitted_height; ++row) {
            std::memcpy(luma + canvas_width * row, source_luma + static_cast<std::size_t>(frame.width) * row, static_cast<std::size_t>(fitted_width));
        }
        for (int row = 0; row < fitted_height / 2; ++row) {
            std::memcpy(chroma + canvas_width * row, source_chroma + static_cast<std::size_t>(frame.width) * row, static_cast<std::size_t>(fitted_width));
        }
        return d.canvas.data();
    }
    d.scaler = sws_getCachedContext(d.scaler, frame.width, frame.height, AV_PIX_FMT_NV12, fitted_width, fitted_height,
                                    AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!d.scaler) {
        return d.canvas.data();
    }
    const std::uint8_t* source[4] = {source_luma, source_chroma, nullptr, nullptr};
    const int source_strides[4] = {frame.width, frame.width, 0, 0};
    std::uint8_t* planes[4] = {luma, chroma, nullptr, nullptr};
    const int strides[4] = {d.width, d.width, 0, 0};
    sws_scale(d.scaler, source, source_strides, 0, frame.height, planes, strides);
    return d.canvas.data();
}

}  // namespace djivcam::media
