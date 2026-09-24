# DJI VCam: task list

Living list, updated as work progresses. Status: `[ ]` to do, `[~]` in progress, `[x]` done.
Milestones refer to [app-architecture.md](app-architecture.md).

## Now

- [~] **Camera controls research**: payload formats for every camera function Mimo exposes
      (from the Mimo APK: decompiled Java, `libacb204_proto.so` = Action 5 Pro protocol definitions,
      `libdjisdk_jni.so` command map; DJI R-SDK docs; Moblin).
- [~] **Virtual webcam, Windows** (milestone 3): Media Foundation virtual camera ("DJI VCam") fed by
      the app through shared memory; visible in OBS, Zoom, Teams, browsers, Windows Camera.
      - [x] Media source COM DLL (IMFMediaSource/IMFMediaStream, NV12 frames)
      - [x] Shared-memory frame transport app -> Frame Server
      - [x] Registration (admin, once) and MFCreateVirtualCamera lifetime handling in the app
      - [x] "Virtual camera" toggle and status in the app; verified with a DirectShow client
      - [ ] Check in OBS, a browser and the Windows Camera app

## Next

- [ ] **Camera controls** (milestone 7), read current values and change them from the app:
      - [ ] Camera mode: video / photo / slow motion / timelapse / hyperlapse
      - [ ] Record start/stop, take photo, recording time
      - [ ] Video resolution, aspect ratio (16:9 / 4:3), frame rate
      - [ ] Stabilization: off / RockSteady / RockSteady+ / HorizonSteady / HorizonBalancing
      - [ ] FOV (wide / dewarp / ...)
      - [ ] Exposure: auto / manual, ISO, shutter speed, EV compensation, metering, anti-flicker
      - [ ] White balance (auto / manual Kelvin)
      - [ ] Color: normal / D-Log M / HLG, sharpness, noise reduction
      - [ ] Audio: microphone, wind noise reduction, levels
      - [ ] Status: battery, storage, temperature, timecode
      - [ ] "Camera settings" panel in the app with live values
- [~] **Windows installer**: Inno Setup script (installs the app, registers the virtual camera,
      Start-menu entry, uninstaller); output `binaries/dji-vcam-setup-<version>.exe` (git-ignored)
      - [x] Installer script, packaging, app-local Visual C++ runtime
      - [ ] Test install, upgrade and uninstall (needs an administrator prompt)
      - [x] App icon (exe, windows, installer) and version info
- [ ] **Replace SimpleBLE** (BUSL-1.1) with our own Bluetooth code: C++/WinRT on Windows, BlueZ
      over D-Bus on Linux, so the project stays freely licensable
- [ ] **Rename** the repository folder and gdrive remote to `dji-vcam` (end of a session)

## Later

- [ ] Linux: v4l2loopback virtual camera, BlueZ, NetworkManager link; test on a real Linux machine
- [ ] Wi-Fi adapter link: detect a second adapter, join the camera AP on it, keep internet routing
- [ ] 1080p30: try the camera's RTMP mode (proven 1080p) received by the app
- [ ] Loss and re-sends: A/B test with the camera on a lossy link, the default (ACK the newest
      datagram at once) against waiting for re-sends (`video/gapWaitMs` = 50 in the app's
      settings); compare "delay", "recovered", "dup" and the seconds of lag seen on 2026-09-25
- [ ] Startup latency: request a keyframe on connect (AppRequestIFrame 0x09/0xA8)
- [ ] Latency: hand GPU frames to preview / virtual camera without CPU copies
- [ ] Decode thread headroom: decode to NV12 (no BGRA round trip), draw NV12 in the preview with
      a shader, and skip the conversions for frames that are already late
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
