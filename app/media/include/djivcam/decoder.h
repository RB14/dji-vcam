// Low-latency H.264 decoding (FFmpeg) for the live view, GPU-accelerated when available.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace djivcam::media {

// A decoded picture in NV12, the GPU decoders' native format: the Y plane (width x height)
// followed by the interleaved UV plane (width x height / 2), rows packed (stride = width).
struct Nv12Frame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> data;
    // How the stream says to turn it into RGB (the camera: BT.709, video range).
    bool bt709 = true;        // else BT.601
    bool full_range = false;  // 0-255 instead of 16-235 luma
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
    std::optional<Nv12Frame> decode(std::span<const std::uint8_t> access_unit);

    // "d3d11va", "vaapi", ... or "software".
    const std::string& backend() const;
    // The graphics adapter decoding ("Intel(R) Iris(R) Xe Graphics") when the backend tells it
    // (Windows D3D11VA; on a laptop with two GPUs, which one), else empty.
    const std::string& gpu() const;
    bool hardware() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Fits frames into a fixed-size NV12 picture (the virtual camera's 1920x1080): scaled to fill it with
// the aspect ratio kept, centered with black bars when the ratio differs (a 4:3 live view).
class Nv12Canvas {
public:
    Nv12Canvas(int width, int height);
    ~Nv12Canvas();
    Nv12Canvas(const Nv12Canvas&) = delete;
    Nv12Canvas& operator=(const Nv12Canvas&) = delete;

    // Returns the picture to publish (Y plane, then the interleaved UV plane): the frame's own data
    // when it already has the canvas size, otherwise the canvas.
    const std::uint8_t* draw(const Nv12Frame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace djivcam::media
