# DJI VCam: task list

Living list, updated as work progresses. Status: `[ ]` to do, `[~]` in progress, `[x]` done.
Milestones refer to [app-architecture.md](app-architecture.md).

## Overhaul: the camera on your Wi-Fi (branch `wifi-live`, started 2026-09-26)

The camera joins a Wi-Fi network in its Live Streaming mode (protocol-notes.md 3.13): the
low-latency live view then works over that network in 1080p, and the camera can also push RTMP to
this PC. This replaces the ESP32 bridge entirely.

Decisions (2026-09-26):
- **Two feeds**: the low-latency preview (our DJI protocol) or RTMP (camera -> RTMP server on this
  PC -> our player, optimised for delay but decoding every frame). RTMP runs only when chosen for
  now; "always stream RTMP" (screens time out, instant switching) is decided after the test session.
- **RTMP server behind an interface**, implemented with go2rtc only (7 MB download, ~20 MB against
  MediaMTX's ~57 MB); MediaMTX (proven with DJI's RTMP client on 2026-09-26) is the fallback if
  go2rtc does not take the camera's stream.
- **The ESP32 goes completely** (code, firmware, tools, docs); it stays in git history and v0.1.0.
- **Networks**: the camera's own scan list plus manual entry (hidden networks); a PC-hotspot mode is
  for later.
- The camera settings panel offers only what Live Streaming mode supports; the RTMP address is shown
  for other apps; the webcam becomes 1080p.

Phases:
- [x] 0. Groundwork: experiment CLI (`--join-network` & co.), the recipe in protocol-notes.md 3.13
- [x] 1. Core protocol (live_stream, live_session with a scripted-camera test, the camera found by
      its MAC) and the Bluetooth live session in the app (CameraConnector: Live Streaming mode,
      join, optional RTMP push, held link, starting over when the link is lost, Video mode on stop)
- [x] 2. RTMP server interface + go2rtc; the RTMP player (network_stream) into the GPU pipeline;
      checked end to end without the camera (go2rtc on loopback, a test pattern, `--stream`)
- [x] 3. App: network dialog (scan list, hidden networks, DPAPI-protected password), feed choice,
      RTMP quality, Stream addresses, settings panel limited to Live Streaming mode
- [x] 4. Webcam in 1080p (shared layout version 2)
- [x] 5. The ESP32 removed: bridge code, the access-point channel feature, `firmware/`, tools
- [x] 6. Docs overhaul: README, installing.md, app-architecture, building, tasks, CHANGELOG;
      camera-controls' Live Streaming matrix
- [x] 7. Installer (go2rtc + license, firewall rule for RTMP); merged and released 0.2.0
      (2026-09-26)
- [x] Camera settings on the RTMP feed, over the Bluetooth link the app holds anyway
      (CameraConnector carries the settings panel's requests and the camera's pushes)

Test session with the camera (2026-09-26):
- [x] The whole app flow on the camera: network dialog, join, the camera found by its MAC, both
      feeds, switching between them (RTMP back to low latency rejoins, ~20 s), Disconnect back to
      Video mode. Connect to video: ~26 s, of which the join ~11 s
- [x] go2rtc with DJI's RTMP client: works, ~0.4 s behind, once the start follows Mimo
      (settings, then the start; an address with an app and a key; at most 127 bytes of JSON)
- [x] The camera's screens time out while it streams RTMP
- [x] Both feeds at once: the live view runs alongside the RTMP push, with every setting over it
      (tested with the CLI); the push has to start first (during a live view the camera ignores
      the RTMP settings, which also broke the low latency -> RTMP switch: now a rejoin)
- [x] Both feeds in the app (the option, on by default): the push first then the live view, the
      switch in both directions at once, every setting on both feeds, the screens turn off; the
      option off runs the live view alone (2026-09-26)
- [ ] Both feeds: the 720p RTMP quality (a size change inside the decoder), a camera restart
- [ ] Heat (the camera's temperature level in the 0x1D/0x02 status push) and battery over 30 min:
      both feeds vs the low-latency feed alone
- [x] Camera settings in Live Streaming mode on the low-latency feed: all of them work; the
      shutter cannot be slower than a frame (1/24 turns into 1/30 at 30 fps)
- [ ] Can the camera record in Live Streaming mode
- [x] Settings on the RTMP feed over Bluetooth: only the 02/8E parameters (stabilization, scene,
      FOV, auto ISO limit); the status topics are pushed over Bluetooth
- [ ] Stop only the RTMP push and stay on the network, so switching back to low latency needs no
      rejoin. `dji-vcam-cli --ble --join-network FILE --live-mode --live-start 1080 URL --hold 90`
      with: `--support-stop-live` and the stop `--ble-send 08,02,8e,01011a000102`; other pid
      `0x001A` values; stopping go2rtc during the push (that makes the camera retry by itself).
      Then check the live view still plays
- [ ] 5 GHz networks; the network list's flag byte
- [x] Camera restart while connected, on each feed (as test 7 below did for 0.1.0): the app finds
      the camera, puts it back on the network and the video returns by itself
- [ ] Sleep and Bluetooth loss (out of range, Bluetooth off): the session rebuilds itself
- [ ] The 1080p webcam (new media source: install), in Windows Camera, Chrome and OBS
- [ ] Full install on a clean profile

Found in the test session (2026-09-26), to do:
- [ ] Join: resend it when the camera does not answer (the first join right after a scan got no
      answer; Mimo sends it twice) instead of failing after 30 s; say "no answer yet: check the
      password" while waiting; scan first to join faster (~1 s after a scan, ~11 s without)
- [ ] Network dialog: "From Windows" fills in the password Windows keeps for this computer's
      network (one administrator prompt); say that an empty password means an open network
- [ ] RTMP start: retry a start without answer; keep its error visible (the "waiting for the RTMP
      stream" status covers it); follow the camera's livestream state (`ee/03`) to say when it
      failed; a hint about the firewall when the camera never connects
- [ ] Status subscriptions over Bluetooth: the camera keeps them across connections until it
      restarts, so every RTMP session adds a set (~55 pushes/s after a few): reuse the ids, or
      read the settings instead
- [ ] Full settings on the RTMP feed: try the live view's connection for commands only while the
      camera pushes
- [ ] RTMP address without the port (for long IP addresses): untested with the camera
- [ ] Answer or ignore the camera's own Bluetooth requests `00/81` and `00/74` (Mimo does not
      seem to answer them either)

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

- [ ] Linux: v4l2loopback virtual camera, BlueZ; test on a real Linux machine
- [ ] Without a Wi-Fi network: start this computer's own hotspot (Windows Mobile Hotspot) and put
      the camera on it (outdoors, no router)
- [x] Both feeds at once (Options → Keep the RTMP stream running alongside the live view, on by
      default): the push first, then the live view; the Feed switches the picture (2026-09-26)
- [ ] Other camera models: test the Action 4/6, Osmo 360, Pocket 3 (the app names them, only 0x15
      tested); prefer known camera models in the Bluetooth search (a DJI Mic could be picked first)
- [x] Versions and releases: SemVer in `project()`, full version from git (`0.1.0-dev+g<commit>`)
      in file names, version resources, About and `--version`; `release-github.sh` publishes the
      installer, ZIP and checksums (v0.1.0, 2026-09-25, also with the bridge firmware)
- [ ] Code-signed installer (SmartScreen warns today)
- [ ] ARM64 build (Windows 11 on ARM): Qt and FFmpeg have ARM64 builds; the media source must be
      ARM64 too. No 32-bit build: Windows 11 has no 32-bit edition
- [ ] Packaging and the tools wrapper in PowerShell too (they need WSL today)
- [ ] `update-vcam-source.ps1` still swaps the ProgramData copy; the installer registers the DLL
      from Program Files
- [x] ~~Wi-Fi adapter link~~, ~~built-in Wi-Fi link~~, ~~Bluetooth from the ESP32~~: replaced by the
      camera joining your network (2026-09-26)
- [x] 1080p: the live view is 1080p in Live Streaming mode; the RTMP feed too (2026-09-26)
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
