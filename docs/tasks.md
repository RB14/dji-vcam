# DJI VCam: task list

Living list, updated as work progresses. Status: `[ ]` to do, `[~]` in progress, `[x]` done.
Milestones refer to [app-architecture.md](app-architecture.md).

## Now

- [~] **Camera controls** (milestone 7): research in [camera-controls.md](camera-controls.md);
      implemented in the app (Camera settings panel) and the CLI (`--camera`, `--camera-set`),
      tested against `tools/fake_camera.py`. **Not yet run against the real camera.**
      - [ ] With the camera: experiments 1-3 of camera-controls.md section 6 (status dump and diff,
            parameter GET sweep, setter round-trips), then fix any layout that differs on the A5P
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
      over D-Bus on Linux, so the project stays freely licensable (branch `ble-native`)
      - [x] Platform layer (`app/ble/src/central.h`) and Windows backend on C++/WinRT; scanning
            and the GUI's connect flow verified without the camera
      - [ ] Verify with the camera (pair, wake, credentials, keepalive), then merge to main
      - [ ] BlueZ backend for Linux (replaces the SimpleBLE stopgap), with the Linux milestone
- [ ] **Rename** the repository folder and gdrive remote to `dji-vcam` (end of a session)

## Later

- [ ] Linux: v4l2loopback virtual camera, BlueZ, NetworkManager link; test on a real Linux machine
- [ ] Wi-Fi adapter link: detect a second adapter, join the camera AP on it, keep internet routing
- [ ] 1080p30: try the camera's RTMP mode (proven 1080p) received by the app; first the cheap
      live-view experiments 4 and 8 of camera-controls.md (09/A8 enable, stream-quality parameter)
- [ ] Loss and re-sends: A/B test with the camera on a lossy link, the default (ACK the newest
      datagram at once) against waiting for re-sends (`video/gapWaitMs` = 50 in the app's
      settings); compare "delay", "recovered", "dup" and the seconds of lag seen on 2026-09-25
- [ ] Startup latency: request a keyframe on connect (AppRequestIFrame 0x09/0xA8)
- [ ] Latency: hand GPU frames to preview / virtual camera without CPU copies
- [ ] After (re)connecting, skip access units until the first keyframe (avoids a few gray frames)
- [ ] Optional OBS "direct mode" plugin reusing the core

## Done

- [x] Proof of concept: live view over the camera's AP through the ESP32-S3 bridge (~135 ms)
- [x] ESP32-S3 USB NCM bridge firmware: DHCP route scrubbing, link state, queued USB pump, OTA
      updates with rollback, link-drop diagnostics
- [x] C++ core (DUML, datalink, reconnecting session, reassembler with gap recovery), tests
- [x] GPU decoding (D3D11VA/DXVA2, VAAPI/CUDA/VDPAU) with CPU fallback
- [x] Qt app: preview, stats, resolution, stage messages, settings, options menu
- [x] Bluetooth in the app: find, pair, wake Wi-Fi, credentials, keepalive, re-wake; bridge setup
- [x] Building and installation guides; portable ZIP packaging into `binaries/`
- [x] Rename of the app code to dji-vcam
- [x] Diagnostics: replay of recordings in the app, decode benchmark in the CLI, in-app delay and
      duplicate counters in the status bar
- [x] NV12 end to end with GPU color conversion in the preview (decode thread 6.9 -> 2.8 ms per
      frame; fixes BT.601 colors on the camera's BT.709 stream)
- [x] Snapshot button (PNG of the current frame in Pictures\DJI VCam)
