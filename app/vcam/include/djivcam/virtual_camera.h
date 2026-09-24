// The "DJI VCam" virtual webcam, seen by OBS, Zoom, Teams, browsers and the Windows Camera app.
//
// Windows 11: registers a Media Foundation virtual camera (MFCreateVirtualCamera) for the lifetime
// of this process and publishes NV12 frames to its media source through shared memory
// (vcam_protocol.h). The media source DLL must be registered once (administrator).
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace djivcam::vcam {

class VirtualCamera {
public:
    VirtualCamera();
    ~VirtualCamera();
    VirtualCamera(const VirtualCamera&) = delete;
    VirtualCamera& operator=(const VirtualCamera&) = delete;

    // True if the media source DLL is registered with Windows.
    static bool source_registered();
    // Installs the media source (administrator; shows a UAC prompt): copies `source_dll` to
    // %ProgramData%\DJI VCam, where the camera services can read it, and registers it there.
    static bool install_source(const std::wstring& source_dll, std::string* error);

    // Makes "DJI VCam" appear as a camera. Returns false with a reason if it cannot.
    bool start(std::string* error);
    void stop();
    bool running() const;

    // Publishes one kWidth x kHeight NV12 frame (Y plane followed by the interleaved UV plane).
    // Frames are dropped until an application opens the camera.
    void publish(const std::uint8_t* nv12);
    // True while an application has the camera open (its media source is running).
    bool in_use() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace djivcam::vcam
