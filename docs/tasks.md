# DJI VCam: task list

Living list, updated as work progresses. Status: `[ ]` to do, `[~]` in progress, `[x]` done.
Milestones refer to [app-architecture.md](app-architecture.md).

## Test plan with the camera (started 2026-09-25)

1. [x] Media source update (lock fix, heartbeat, BT.709 type) installed. A stale Windows build
       (MSBuild ignores header changes under AppData) crashed it first: the work folder moved to
       `%USERPROFILE%\.dji-vcam`.
2. [ ] Installer: install, start from the Start menu, uninstall (needs administrator prompts)
3. [x] Live view on main: webcam carries the live camera; loss and noise investigated (camera never
       re-sends, no keyframe request, bridge firmware 0.4.0 cut the loss); freeze-after-loss option
4. [x] Camera controls: status topics decode correctly on the camera, the EV byte matches the camera
       screen, settings changed in the panel and on the camera follow each other (1 s refresh)
5. [x] Native Bluetooth branch (`ble-native`): pairs, wakes and fetches the credentials on the
       camera; merged
6. [ ] Webcam consumers: OBS (set "Use buffering" off), a browser, Windows Camera
7. [x] Camera power cycle while connected: the app finds, wakes and streams again by itself, ~15 s
       after the camera is on. It used to get no video after a short power-off until the camera had
       been off for a minute: the app kept its Bluetooth link open, DJI Mimo hangs up after the wake
       (protocol-notes.md 3.11)

## Now

- [~] **Camera controls** (milestone 7): research in [camera-controls.md](camera-controls.md);
      implemented in the app (Camera settings panel) and the CLI (`--camera`, `--camera-set`),
      verified on the camera both ways (camera-controls.md 5b)
      - [x] Mode, record start/stop, photo, recording time
      - [x] Format (resolution with aspect ratio, frame rate), codec
      - [x] Stabilization, Daily/Sport, FOV
      - [x] Exposure mode, ISO, shutter, EV, auto ISO limit, anti-flicker
      - [x] White balance (auto / Kelvin), color profile, texture, noise reduction
      - [x] Status: battery, storage, metered ISO/shutter
      - [ ] Status: temperature (push not identified yet), timecode
      - [ ] Audio: mic channel (parameter 0x0020, set unconfirmed); wind noise is not exposed by Mimo
      - [ ] Timelapse / hyperlapse parameters, photo size/format/burst, custom modes, loop recording
      - [ ] Slow-motion speed (format with speed ratio), AE lock, spot metering, HDR video
- [~] **Virtual webcam, Windows** (milestone 3): Media Foundation virtual camera ("DJI VCam") fed by
      the app through shared memory; visible in OBS, Zoom, Teams, browsers, Windows Camera.
      - [x] Media source COM DLL (IMFMediaSource/IMFMediaStream, NV12 frames)
      - [x] Shared-memory frame transport app -> Frame Server
      - [x] Registration (admin, once) and MFCreateVirtualCamera lifetime handling in the app
      - [x] "Virtual camera" toggle and status in the app; verified with a DirectShow client
      - [ ] Check in OBS, a browser and the Windows Camera app

## Next

- [~] **Windows installer**: Inno Setup script (installs the app, registers the virtual camera,
      Start-menu entry, uninstaller); output `binaries/dji-vcam-setup-<version>.exe` (git-ignored)
      - [x] Installer script, packaging, app-local Visual C++ runtime
      - [ ] Test install, upgrade and uninstall (needs an administrator prompt)
      - [x] App icon (exe, windows, installer) and version info
- [~] **Replace SimpleBLE** (BUSL-1.1) with our own Bluetooth code: C++/WinRT on Windows, BlueZ
      over D-Bus on Linux, so the project stays freely licensable
      - [x] Platform layer (`app/ble/src/central.h`) and Windows backend on C++/WinRT, verified
            with the camera and merged; it really ends the link on disconnect (SimpleBLE did not)
      - [ ] BlueZ backend for Linux (replaces the SimpleBLE stopgap), with the Linux milestone
- [ ] **Rename** the repository folder and gdrive remote to `dji-vcam` (end of a session)

## Later

- [ ] Linux: v4l2loopback virtual camera, BlueZ, NetworkManager link; test on a real Linux machine
- [ ] Wi-Fi adapter link: detect a second adapter, join the camera AP on it, keep internet routing;
      a 5 GHz USB dongle is the way to 5 GHz (the ESP32-C5 has no usable USB device mode in ESP-IDF,
      so it cannot replace the S3 as a USB network adapter)
- [ ] 1080p30: try the camera's RTMP mode (proven 1080p) received by the app; first the cheap
      live-view experiments 4 and 8 of camera-controls.md (09/A8 enable, stream-quality parameter)
- [ ] Latency: hand GPU frames to preview / virtual camera without CPU copies
- [ ] Optional OBS "direct mode" plugin reusing the core

## Done

- [x] Proof of concept: live view over the camera's AP through the ESP32-S3 bridge (~135 ms)
- [x] ESP32-S3 USB NCM bridge firmware: DHCP route scrubbing, link state, queued USB pump, OTA
      updates with rollback, link-drop diagnostics
- [x] C++ core (DUML, datalink, reconnecting session, reassembler with gap recovery), tests
- [x] GPU decoding (D3D11VA/DXVA2, VAAPI/CUDA/VDPAU) with CPU fallback
- [x] Qt app: preview, stats, resolution, stage messages, settings, options menu
- [x] Bluetooth in the app: find, pair, wake Wi-Fi, credentials, then hang up as DJI Mimo does;
      re-wake when the camera's network is gone; bridge setup
- [x] Lost video handled in real time (the camera never re-sends, no keyframe request exists:
      protocol-notes.md 3.10); the video area shows the connection state instead of a frozen frame
- [x] Recovery after a camera power cycle (protocol-notes.md 3.11); app log file in
      `%LOCALAPPDATA%\dji-vcam\dji-vcam\logs`
- [x] Building and installation guides; portable ZIP packaging into `binaries/`
- [x] Rename of the app code to dji-vcam
- [x] Diagnostics: replay of recordings in the app, decode benchmark in the CLI, in-app delay and
      duplicate counters in the status bar
- [x] NV12 end to end with GPU color conversion in the preview (decode thread 6.9 -> 2.8 ms per
      frame; fixes BT.601 colors on the camera's BT.709 stream)
- [x] Snapshot button (PNG of the current frame in Pictures\DJI VCam)
