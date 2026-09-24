// Low-latency H.264 decoding (FFmpeg) for the live view, GPU-accelerated when available.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace djivcam::media {

// A decoded picture in 32-bit BGRA (the memory layout of Qt's QImage::Format_RGB32).
struct BgraFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<std::uint8_t> pixels;
};

enum class DecoderPreference {
    Auto,      // first working GPU backend, else CPU
    Hardware,  // GPU only (throws if none works)
    Software,  // CPU only
};

// Fed one access unit at a time; configured for minimum delay (no frame threading, no
// reordering), which suits the camera's I/P-only live-view stream.
//
// GPU backends are probed in order: Windows D3D11VA, DXVA2; Linux VAAPI (Intel/AMD),
// CUDA/NVDEC (NVIDIA), VDPAU; macOS VideoToolbox.
class H264Decoder {
public:
    explicit H264Decoder(DecoderPreference preference = DecoderPreference::Auto);
    ~H264Decoder();
    H264Decoder(const H264Decoder&) = delete;
    H264Decoder& operator=(const H264Decoder&) = delete;

    // Returns the picture this access unit completes, if any. Corrupt units are skipped.
    std::optional<BgraFrame> decode(std::span<const std::uint8_t> access_unit);

    // "d3d11va", "vaapi", ... or "software".
    const std::string& backend() const;
    bool hardware() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace djivcam::media
