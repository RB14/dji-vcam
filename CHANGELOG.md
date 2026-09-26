# Changelog

Versions follow [Semantic Versioning](https://semver.org); see docs/building.md "Versions and
releases". The ESP32-S3 bridge firmware has its own version, given with each release.

## Unreleased

The camera on your Wi-Fi: no ESP32 board any more.

- The camera joins your Wi-Fi network: over Bluetooth the app switches it to its Live Streaming
  mode and has it join a network this computer is on, picked from the networks the camera hears or
  typed (hidden networks); the password is kept encrypted for your Windows account.
- The low-latency feed is now 1080p (the camera's live view in Live Streaming mode), with the same
  ~135 ms delay.
- New RTMP feed: the camera pushes RTMP to go2rtc, bundled with the app, and the app plays it
  (~0.4 s behind); other apps can open it too (Options → Stream addresses).
- The DJI VCam webcam is 1920x1080; smaller video is scaled up.
- While connected, the shooting mode, format and codec controls are hidden (Live Streaming mode).
  On the RTMP feed the settings go over Bluetooth, where the camera takes stabilization, scene, FOV
  and the auto ISO limit; exposure and color are changed on the low-latency feed.
- The manual shutter list stops at the frame rate (1/30 at 30 fps), the slowest the camera takes.
- Removed: the ESP32-S3 bridge (firmware, tools) and the camera access point channel option.

## 0.1.0 (2026-09-25)

First public preview, for Windows 11 and the DJI Osmo Action 5 Pro (the only camera tested).

- One-click connection: Bluetooth pairing and wake (the app hangs up right after, as DJI Mimo
  does), the ESP32-S3 bridge joins the camera's Wi-Fi, the live view starts; after a camera power
  cycle the video comes back by itself.
- Live view: H.264 1280x720 at 30 fps, decoded on the GPU, about 135 ms glass to glass.
- **DJI VCam** webcam for every app (OBS, browsers, Zoom, Teams, Windows Camera), registered for
  all users by the installer, paced to the camera's frames.
- Camera settings panel with DJI Mimo's main controls (mode, recording, format, stabilization,
  exposure, white balance, color) and status (battery, storage).
- Automatic choice of a quiet 2.4 GHz Wi-Fi channel for the camera.
- Status bar: resolution, Wi-Fi channel, frame rate, bitrate, delay, loss, and the decoding GPU;
  the toolbar names the camera model.
- Installer (x64) with uninstaller; portable ZIP; bridge firmware image.
