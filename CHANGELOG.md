# Changelog

Versions follow [Semantic Versioning](https://semver.org); see docs/building.md "Versions and
releases". The ESP32-S3 bridge firmware has its own version, given with each release.

## Unreleased

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
