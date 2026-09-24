# DJI VCam: task list

Living list, updated as work progresses. Status: `[ ]` to do, `[~]` in progress, `[x]` done.
Milestones refer to [app-architecture.md](app-architecture.md).

## Now

- [~] **Camera controls research**: payload formats for every camera function Mimo exposes
      (from the Mimo APK: decompiled Java, `libacb204_proto.so` = Action 5 Pro protocol definitions,
      `libdjisdk_jni.so` command map; DJI R-SDK docs; Moblin).
- [ ] **Virtual webcam, Windows** (milestone 3): Media Foundation virtual camera ("DJI VCam") fed by
      the app through shared memory; visible in OBS, Zoom, Teams, browsers, Windows Camera.
      - [ ] Media source COM DLL (IMFMediaSource/IMFMediaStream, NV12 frames)
      - [ ] Shared-memory frame transport app -> Frame Server
      - [ ] Registration (admin, once) and MFCreateVirtualCamera lifetime handling in the app
      - [ ] "Virtual camera" toggle and status in the app; test in OBS and a browser

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
- [ ] **Windows installer**: Inno Setup script (installs the app, registers the virtual camera,
      Start-menu entry, uninstaller); output `binaries/dji-vcam-setup-<version>.exe` (git-ignored)
- [ ] **Replace SimpleBLE** (BUSL-1.1) with our own Bluetooth code: C++/WinRT on Windows, BlueZ
      over D-Bus on Linux, so the project stays freely licensable
- [ ] **Rename** the repository folder and gdrive remote to `dji-vcam` (end of a session)

## Later

- [ ] Linux: v4l2loopback virtual camera, BlueZ, NetworkManager link; test on a real Linux machine
- [ ] Wi-Fi adapter link: detect a second adapter, join the camera AP on it, keep internet routing
- [ ] 1080p30: try the camera's RTMP mode (proven 1080p) received by the app
- [ ] Noise: confirm whether the camera re-sends lost datagrams (watch "recovered" counts)
- [ ] Startup latency: request a keyframe on connect (AppRequestIFrame 0x09/0xA8)
- [ ] Latency: hand GPU frames to preview / virtual camera without CPU copies
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
