// The "DJI VCam" virtual webcam, seen by OBS, Zoom, Teams, browsers and the Windows Camera app.
//
// Windows 11: a Media Foundation virtual camera (MFCreateVirtualCamera) registered once, for good
// (system lifetime): the installer registers it for all users, a portable copy of the app for the
// current user. It stays listed while the app is closed (its media source then shows a gray "no
// signal" picture) and keeps its Windows settings, e.g. "Allow multiple apps". The app publishes
// NV12 frames to the media source through shared memory (vcam_protocol.h). The media source DLL
// must be registered once (administrator).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

    // The cameras apps can see (Media Foundation video capture devices), by friendly name.
    static std::vector<std::wstring> list_cameras();
    // True if a camera named "DJI VCam" is listed (registered by the installer or by the app).
    static bool camera_registered();
    // Registers "DJI VCam" for good: for all users (administrator) or the current user.
    static bool register_camera(bool all_users, std::string* error);
    // Removes the "DJI VCam" registered with the same scope.
    static bool unregister_camera(bool all_users, std::string* error);

    // Starts / stops sending frames to the webcam (it shows "no signal" meanwhile).
    void start();
    void stop();
    bool running() const;

    // Publishes one kWidth x kHeight NV12 frame (Y plane followed by the interleaved UV plane).
    void publish(const std::uint8_t* nv12);
    // True while an application takes frames from the camera (needs the matching media source).
    bool in_use() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace djivcam::vcam
