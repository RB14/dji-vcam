# Desktop app: architecture and plan

A small desktop app ("Mimo for Desktop"), **DJI VCam** (`dji-vcam`), for Windows and Linux. It
puts a DJI Osmo Action camera on your Wi-Fi network, shows its live view, and exposes it as a
**virtual camera**, so any application (OBS, Zoom, Teams, Discord, browsers) can use the feed.

## Goals

- One click from "camera on" to "webcam available in every app", with no extra hardware.
- Latency close to the raw pipeline (~135 ms glass to glass, measured).
- 1080p: the camera's live view is 1080p in its Live Streaming mode; an RTMP feed for other apps.
- Windows 11 22H2+ and Linux.

## How the camera is reached

Up to 0.1.0 the camera offered its own access point and an ESP32-S3 USB bridge joined it for the
computer. Since 0.2.0 the app switches the camera to its **Live Streaming mode** over Bluetooth and
has it **join a network the computer is on** (docs/protocol-notes.md 3.13); the live view then
works over that network, in 1080p, and the camera can also push RTMP to this computer. The
Bluetooth link stays up for the whole session: the camera leaves the network when it ends.

## Stack

C++20, CMake, Qt 6.8 LTS (GUI only), FFmpeg (H.264 decode, the RTMP feed's RTSP input), Bluetooth
LE through Windows.Devices.Bluetooth (C++/WinRT) on Windows and, for now, SimpleBLE (BlueZ) on
Linux, GoogleTest. go2rtc (a separate MIT program, bundled) is the RTMP server. The Python tools in
`tools/` remain the reference implementation and test bench.

## Components

```
app/
  core/        static library, no Qt: everything that talks to the camera
    duml            DUML framing + CRCs (same test vectors as tools/test_duml.py)
    datalink        UDP 9004 transport: handshake, window ACKs, command packets
    session         the live view: wait for route -> poke -> handshake -> register ->
                    live-view trigger -> heartbeat/ACK loop -> reconnect on silence
    h264            reassembles video datagrams into access units, drops DJI's 0xFF units
    camera_*        camera settings: DUML command builders, status-topic parsers, and a
                    controller that keeps them in sync over the session (camera-controls.md)
    live_stream     Live Streaming mode commands: the mode, the network scan and join, the RTMP
                    start/stop and stored settings, the keep-alive (builders and parsers)
    live_session    their order over a Bluetooth link (live::Link): mode -> join -> optional
                    RTMP push -> keep-alive -> back to Video mode; tested with a scripted camera
    net             sockets; the camera's address on the network from its Wi-Fi MAC
    camera_model    the model byte of the Bluetooth advertisement -> "Osmo Action 5 Pro"
  ble/         Bluetooth LE: scan, connect, pairing (one on-camera approval), requests; also the
               live::Link of live_session (C++/WinRT on Windows, SimpleBLE on Linux)
  media/       FFmpeg: decoder (low-delay flags, GPU first) to NV12 frames; network_stream
               reads the RTMP feed from the local RTMP server (RTSP, no buffering)
  vcam/
    windows/   Media Foundation virtual camera "DJI VCam" (MFCreateVirtualCamera, Windows 11):
               source/ is the COM media source DLL (djivcam-source.dll, adapted from
               smourier/VCamSample) that the Windows Camera Frame Server loads. Its session-0
               instance creates a Global shared-memory section (NV12 1920x1080, 3 seqlock
               slots, see include/djivcam/vcam_protocol.h) which the app opens and writes;
               instances loaded inside apps read the app's Local section instead. The installer
               registers the camera for all users, for good (MFVirtualCameraLifetime_System).
    linux/     v4l2loopback writer (/dev/videoN), the mechanism OBS's Linux virtual camera uses
  gui/         Qt Widgets app: camera_connector (the Bluetooth session on its own thread; it also
               carries the camera settings on the RTMP feed), network_dialog, rtmp_server (the
               interface) + go2rtc_server, pipeline (the low-latency feed, the RTMP feed or a
               replay -> decoder -> preview + webcam), camera_panel, main_window
  cli/         dji-vcam-cli: every step on its own, for experiments and diagnostics
  tests/
```

Threads: the connector's Bluetooth thread (it holds the link with the keep-alive), the session
thread (UDP receive loop + timers) or the RTMP reader, and a decode thread, which hands each NV12
frame to the virtual camera (as is, or scaled into 1920x1080) and, shared and unchanged, to the
preview. The preview uploads the Y and UV planes as two textures and converts them to RGB in a
shader (BT.709 or BT.601, video or full range, as the stream signals), so the CPU neither converts
colors nor scales. About 3-5 ms of CPU per frame on an Intel iGPU laptop.

The RTMP server is an interface (`RtmpServer`): the implementation runs its program as a child
process with a generated configuration (only RTMP listens on the network) and polls its HTTP API
for the camera's connection. go2rtc was chosen for its size (~20 MB against MediaMTX's ~57 MB);
another server replaces it by implementing the same few methods.

## Milestones

1. **Core + preview (Windows).** Done: duml, datalink, session, H.264 reassembly, FFmpeg decode.
2. **Bluetooth in the app.** Done: pairing UI, wake, reconnect when the camera sleeps.
3. **Windows virtual camera.** Done: "DJI VCam" shows up in Media Foundation and DirectShow
   apps (OBS, Zoom, Chrome), now in 1080p.
4. **Linux.** v4l2loopback sink, BlueZ; needs a real Linux machine for testing (WSL has neither
   Bluetooth nor v4l2loopback).
5. **The camera on your Wi-Fi** (0.2.0). Done: Live Streaming mode, network join and choice, the
   camera found by its MAC, the low-latency and RTMP feeds; the ESP32 bridge removed.
6. **Quality.** 1080p done (Live Streaming mode); GPU decode done (D3D11VA/VAAPI/NVDEC with CPU
   fallback); still to do: zero-copy GPU frames to the preview and virtual camera.
7. **Camera controls.** Everything Mimo exposes, researched in [camera-controls.md](camera-controls.md):
   `core` camera_protocol and camera_controller, the GUI's Camera settings panel and the CLI's
   `--camera` / `--camera-set`; verified on the camera both ways (2026-09-25). In Live Streaming
   mode the shooting mode, format and codec are hidden; on the RTMP feed the settings go over the
   Bluetooth link, where the camera takes only its parameter settings (camera-controls.md 3.13).

## Open questions

- Both feeds at once (the default since 0.2.0: the screens time out, every setting on both feeds,
  instant switching): its cost in heat and battery is to be measured.
- Stopping only the RTMP push, so switching back to the low-latency feed needs no rejoin
  (docs/protocol-notes.md 3.13).
- Recording while in Live Streaming mode; 5 GHz networks; which settings the camera accepts in
  this mode (docs/tasks.md, test session).
- Latency cost of the virtual camera hop (expected +1-2 frames); a thin OBS "direct mode" plugin
  reusing `core/` can remove it if needed.
