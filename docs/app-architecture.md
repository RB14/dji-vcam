# Desktop app: architecture and plan

A small desktop app ("Mimo for Desktop"), **DJI VCam** (`dji-vcam`), for Windows and Linux. It
connects to a DJI Osmo Action camera over its own Wi-Fi, shows the live view, and exposes it as a
**virtual camera**, so any application (OBS, Zoom, Teams, Discord, browsers) can use the feed.

## Goals

- One click from "camera on" to "webcam available in every app".
- Latency close to the raw pipeline (~135 ms measured with the Python proof of concept).
- The network link is swappable: the ESP32-S3 USB bridge today, a second Wi-Fi adapter later
  (detected and configured by the app), possibly the camera's RTMP mode for 1080p.
- Windows 11 22H2+ and Linux.

## Stack

C++20, CMake, Qt 6.8 LTS (GUI only), FFmpeg (H.264 decode), SimpleBLE (Bluetooth LE on
Windows/WinRT and Linux/BlueZ), GoogleTest. The Python tools in `tools/` remain the reference
implementation and test bench.

## Components

```
app/
  core/        static library, no Qt: everything that talks to the camera
    duml            DUML framing + CRCs (same test vectors as tools/test_duml.py)
    datalink        UDP 9004 transport: handshake, window ACKs, command packets
    session         state machine: wait for route -> poke -> handshake -> register ->
                    live-view trigger -> heartbeat/ACK loop -> reconnect on silence
    h264            reassembles video datagrams into access units, drops DJI's 0xFF units
    decoder         FFmpeg decode (low-delay flags, GPU first) to NV12 frames
    ble             pairing (one on-camera approval), AP wake, AP credentials
    link            how the host reaches 192.168.2.1:
                      bridge   - ESP32-S3 USB NCM bridge (credentials over its console)
                      adapter  - a second Wi-Fi adapter (Windows WLAN API / NetworkManager)
  vcam/
    windows/   Media Foundation virtual camera "DJI VCam" (MFCreateVirtualCamera, Windows 11):
               source/ is the COM media source DLL (djivcam-source.dll, adapted from
               smourier/VCamSample) that the Windows Camera Frame Server loads. Its session-0
               instance creates a Global shared-memory section (NV12 1280x720, 3 seqlock
               slots, see include/djivcam/vcam_protocol.h) which the app opens and writes;
               instances loaded inside apps read the app's Local section instead. The app
               registers the camera for its own lifetime (MFVirtualCameraLifetime_Session).
    linux/     v4l2loopback writer (/dev/videoN), the mechanism OBS's Linux virtual camera uses
  gui/         Qt Widgets app: preview, connect/pair, link selection, settings, stats, tray icon
  tests/
```

Threads: a session thread (UDP receive loop + timers) and a decode thread, which hands each NV12
frame to the virtual camera (as is, or letterboxed into 1280x720) and, shared and unchanged, to
the preview. The preview uploads the Y and UV planes as two textures and converts them to RGB in
a shader (BT.709 or BT.601, video or full range, as the stream signals), so the CPU neither
converts colors nor scales. About 3 ms of CPU per frame on an Intel iGPU laptop.

## Milestones

1. **Core + preview (Windows).** duml, datalink, session, H.264 reassembly, FFmpeg decode, a Qt
   window showing the live view. The camera AP is still woken with `tools/dji_ble.py`.
2. **Bluetooth in the app.** Pairing UI, automatic AP wake, reconnect when the camera sleeps.
3. **Windows virtual camera.** Done: "DJI VCam" shows up in Media Foundation and DirectShow
   apps (OBS, Zoom, Chrome), fed with the live view.
4. **Linux.** v4l2loopback sink, BlueZ, NetworkManager; needs a real Linux machine for testing
   (WSL has neither Bluetooth nor v4l2loopback).
5. **Wi-Fi adapter link.** Detect a second adapter, join the camera AP on it only, keep the
   internet route on the primary adapter.
6. **Quality.** 1080p30 target: remaining routes are the camera's RTMP mode (proven 1080p) and
   further datalink experiments; GPU decode (done: D3D11VA/VAAPI/NVDEC with CPU fallback),
   zero-copy GPU frames to the preview and virtual camera.
7. **Camera controls.** Everything Mimo exposes: resolution/aspect/frame rate, stabilization
   (RockSteady/HorizonSteady...), FOV, exposure (mode, ISO, shutter, EV), white balance, color,
   audio, photo/record. Starting points: `docs/reference/duml-command-map.txt`, the DDS topics the
   camera publishes on `0x00/0x99`, Moblin's `0x02/0x8E` stabilization payload.

## Deliverables after milestone 1

1. **Compilation guide**: building the app on Windows (MSVC, Qt, FFmpeg, from WSL or natively)
   and Linux, plus the ESP32 bridge firmware.
2. **Installation guide**: for users: installing the app, pairing the camera, choosing the link
   (ESP32 bridge / Wi-Fi adapter), using the virtual camera in OBS and other apps.
3. **Installer**: scripted packaging (Windows installer; Linux package/AppImage). Outputs go to
   `binaries/`, which is git-ignored; only the packaging scripts are committed.
4. **Rename the project** to `dji-vcam`. Done for the app, targets and C++ namespace (`djivcam`);
   the repository folder and gdrive remote still to be renamed.

## Open questions

- Is there a live-view resolution/bitrate switch in the datalink protocol? (Under investigation:
  Mimo APK analysis and controlled experiments on the start-sequence payloads.)
- Latency cost of the virtual camera hop (expected +1-2 frames); a thin OBS "direct mode" plugin
  reusing `core/` can remove it if needed.
