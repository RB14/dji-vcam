# DJI VCam: task list

Living list, updated as work progresses. Status: `[ ]` to do, `[~]` in progress, `[x]` done.
Milestones refer to [app-architecture.md](app-architecture.md).

## Test plan with the camera (started 2026-09-25)

1. [x] Media source update (lock fix, heartbeat, BT.709 type) installed. A stale Windows build
       (MSBuild ignores header changes under AppData) crashed it first: the work folder moved to
       `%USERPROFILE%\.dji-vcam`.
2. [x] Installer: install, upgrade over a running app (3 times), uninstall; the webcam is now
       registered for all users for good by the installer, and the uninstaller removes it (the
       webcam, COM registration, firewall rule and files are gone)
3. [x] Live view on main: webcam carries the live camera; loss and noise investigated (camera never
       re-sends, no keyframe request, bridge firmware 0.4.0 cut the loss); freeze-after-loss option
4. [x] Camera controls: status topics decode correctly on the camera, the EV byte matches the camera
       screen, settings changed in the panel and on the camera follow each other (1 s refresh)
5. [x] Native Bluetooth branch (`ble-native`): pairs, wakes and fetches the credentials on the
       camera; merged
6. [x] Webcam consumers: OBS, Chrome (1280x720 @ 30 fps), Windows Camera
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
- [x] **Virtual webcam, Windows** (milestone 3): Media Foundation virtual camera ("DJI VCam") fed by
      the app through shared memory; visible in OBS, Zoom, Teams, browsers, Windows Camera.
      - [x] Media source COM DLL (IMFMediaSource/IMFMediaStream, NV12 frames)
      - [x] Shared-memory frame transport app -> Frame Server
      - [x] Registration for all users and for good by the installer (MF system lifetime); the app
            only publishes frames
      - [x] "Virtual camera" toggle and status in the app; verified with a DirectShow client
      - [x] Checked in OBS, Chrome and the Windows Camera app; paced to the camera's 30 fps

## Next

- [x] **Camera Wi-Fi channel**: `07/2B` moves the camera's access point (protocol-notes.md 3.12);
      Options → Camera Wi-Fi channel (automatic from the bridge's scan, or 1 / 6 / 11), applied in
      the Bluetooth session before the live view; `dji-vcam-cli --ble --wifi-channel N`
- [x] **Webcam pacing**: the media source handed out a sample whenever asked (Chrome measured
      52 fps on a 30 fps camera, repeated frames); it now waits for the app's next frame: Chrome
      measures ~30 fps (2026-09-25)
- [x] **Windows installer**: Inno Setup script (installs the app, registers the virtual camera,
      Start-menu entry, uninstaller); output `binaries/dji-vcam-setup-<version>-x64.exe` (git-ignored)
      - [x] Installer script, packaging, app-local Visual C++ runtime
      - [x] Tested install, upgrade and uninstall (2026-09-25)
      - [x] App icon (exe, windows, installer) and version info
- [~] **Replace SimpleBLE** (BUSL-1.1) with our own Bluetooth code: C++/WinRT on Windows, BlueZ
      over D-Bus on Linux, so the project stays freely licensable
      - [x] Platform layer (`app/ble/src/central.h`) and Windows backend on C++/WinRT, verified
            with the camera and merged; it really ends the link on disconnect (SimpleBLE did not)
      - [ ] BlueZ backend for Linux (replaces the SimpleBLE stopgap), with the Linux milestone
- [x] **Rename** the repository folder, gdrive remote and tools wrapper to `dji-vcam` (2026-09-25)

## Later

- [ ] Linux: v4l2loopback virtual camera, BlueZ, NetworkManager link; test on a real Linux machine
- [ ] Built-in Wi-Fi link: join the camera AP with the computer's own Wi-Fi (no bridge; no Wi-Fi
      internet while connected, fine with Ethernet)
- [ ] Bluetooth wake from the ESP32 bridge (the S3 has BLE): no Bluetooth needed on the computer
- [ ] Other camera models: test the Action 4/6, Osmo 360, Pocket 3 (the app names them, only 0x15
      tested); prefer known camera models in the Bluetooth search (a DJI Mic could be picked first)
- [x] Versions and releases: SemVer in `project()`, full version from git (`0.1.0-dev+g<commit>`)
      in file names, version resources, About and `--version`; `release-github.sh` publishes the
      installer, ZIP, bridge firmware image and checksums (v0.1.0, 2026-09-25)
- [ ] Code-signed installer (SmartScreen warns today)
- [ ] ARM64 build (Windows 11 on ARM): Qt and FFmpeg have ARM64 builds; the media source must be
      ARM64 too. No 32-bit build: Windows 11 has no 32-bit edition
- [ ] Packaging and the tools wrapper in PowerShell too (they need WSL today)
- [ ] `update-vcam-source.ps1` still swaps the ProgramData copy; the installer registers the DLL
      from Program Files
- [ ] Wi-Fi adapter link: detect a second adapter, join the camera AP on it, keep internet routing;
      a 5 GHz USB dongle is the way to 5 GHz (the ESP32-C5 has no usable USB device mode in ESP-IDF,
      so it cannot replace the S3 as a USB network adapter)
- [ ] 1080p30: try the camera's RTMP mode (proven 1080p) received by the app; first the cheap
      live-view experiments 4 and 8 of camera-controls.md (09/A8 enable, stream-quality parameter)
- [ ] Latency: hand GPU frames to preview / virtual camera without CPU copies
- [ ] Optional OBS "direct mode" plugin reusing the core
- [ ] Multi-app webcam: check that Windows 11's "Allow multiple apps" (Settings → Cameras → DJI VCam
      → Advanced) works for DJI VCam and stays on (deferred 2026-09-25; it cannot be enabled by the
      installer: its storage is undocumented)

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
