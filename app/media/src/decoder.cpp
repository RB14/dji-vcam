#include "djivcam/decoder.h"

#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

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
        return packet && frame && downloaded;
    }

    BgraFrame to_bgra(const AVFrame& picture) {
        scaler = sws_getCachedContext(scaler, picture.width, picture.height,
                                      static_cast<AVPixelFormat>(picture.format), picture.width, picture.height,
                                      AV_PIX_FMT_BGRA, SWS_POINT, nullptr, nullptr, nullptr);
        BgraFrame out;
        out.width = picture.width;
        out.height = picture.height;
        out.stride = picture.width * 4;
        out.pixels.resize(static_cast<std::size_t>(out.stride) * static_cast<std::size_t>(out.height));
        std::uint8_t* planes[4] = {out.pixels.data(), nullptr, nullptr, nullptr};
        const int strides[4] = {out.stride, 0, 0, 0};
        sws_scale(scaler, picture.data, picture.linesize, 0, picture.height, planes, strides);
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

bool H264Decoder::hardware() const { return impl_->device != nullptr; }

std::optional<BgraFrame> H264Decoder::decode(std::span<const std::uint8_t> access_unit) {
    Impl& d = *impl_;
    d.input.assign(access_unit.begin(), access_unit.end());
    d.input.resize(access_unit.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    d.packet->data = d.input.data();
    d.packet->size = static_cast<int>(access_unit.size());
    if (avcodec_send_packet(d.context, d.packet) < 0) {
        return std::nullopt;
    }
    std::optional<BgraFrame> latest;
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
        latest = d.to_bgra(*picture);
        av_frame_unref(d.downloaded);
        av_frame_unref(d.frame);
    }
    return latest;
}

}  // namespace djivcam::media
