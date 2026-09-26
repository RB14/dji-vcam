# DJI VCam

Use a **DJI Osmo Action 5 Pro** as a wireless, low-latency camera on your computer: in **OBS** and,
through a virtual webcam, in any other app. DJI VCam speaks the protocol the DJI Mimo app uses for
its live preview, and can also take the camera's RTMP stream.

> Status (2026-09-26): the Windows app puts the camera on your Wi-Fi network over Bluetooth and plays
> its live view in **1080p** with **~135 ms glass-to-glass** latency, decoded on the GPU, or its RTMP
> stream (~0.4 s) through a small RTMP server on this computer. Either one becomes the **DJI VCam**
> webcam for other apps. No extra hardware: the ESP32-S3 bridge of 0.1.0 is gone. A Camera settings
> panel exposes Mimo's controls; a Windows installer is built by `app/scripts/package-windows.sh`.
> See [docs/app-architecture.md](docs/app-architecture.md) and [docs/tasks.md](docs/tasks.md).

## Requirements

> **Tested only with the DJI Osmo Action 5 Pro.** Other DJI cameras may work or may need changes:
> see [Other DJI cameras](#other-dji-cameras).

| What | Why |
|---|---|
| **DJI Osmo Action 5 Pro**, activated once with DJI Mimo | The camera the app is developed and tested with |
| **Windows 11** | The webcam uses Windows 11's virtual camera API (Media Foundation `MFCreateVirtualCamera`), which Windows 10 does not have. Linux support is in progress |
| **Bluetooth LE** on the computer | Pairs the computer with the camera (one approval on the camera screen), switches the camera to its Live Streaming mode and tells it which Wi-Fi network to join. The link stays up while connected: the camera leaves the network when it ends. A desktop without Bluetooth needs a USB Bluetooth adapter (any adapter Windows supports should do; not tested yet) |
| **A Wi-Fi network the camera and this computer share** | The camera joins it; the computer can be on it over Wi-Fi or Ethernet. Not a guest network that keeps its devices apart. 2.4 GHz (5 GHz is not tested yet) |
| A GPU (optional) | Decoding uses the GPU when there is one (D3D11VA), else the CPU |

### How the camera connects

The camera normally offers its live view only on its **own Wi-Fi access point**. In its Live
Streaming mode, the one DJI Mimo uses for RTMP livestreams, it instead joins an existing network,
and its live view works there too (in 1080p). So DJI VCam asks it over Bluetooth to switch to that
mode and join your network, finds it there, and plays either:

- the **low-latency feed**: the camera's live view, the protocol DJI Mimo uses for its preview
  (~0.15 s behind, 1080p); or
- the **RTMP feed**: the camera pushes RTMP to the RTMP server bundled with the app (go2rtc), which
  other apps (OBS, VLC) can open too (~0.4 s behind: the camera buffers its livestream).

Your computer keeps its internet connection, and no extra hardware is involved. While connected,
the camera shows "Preparing to live stream" and does not record.

### Other DJI cameras

The app reads the model from the camera's Bluetooth advertisement and shows it in the toolbar
(*Camera: Osmo Action 5 Pro (…)*); it knows the Osmo Action 2, 3, 4, 5 Pro and 6, the Osmo 360 and
the Osmo Pocket 3 and 4, and warns when the camera is not the tested Action 5 Pro. These cameras
belong to the same family: DJI Mimo drives them with the same SDK, and the Pocket 3 streams its
preview over its access point the same way (as the open-source PocketShow shows). So they may work,
or need small changes (a model-specific command, another Bluetooth detail); nobody has tried yet.
Reports are welcome.

## Installation

1. **Download** `dji-vcam-setup-<version>-x64.exe` from the
   [latest release](https://github.com/RB14/dji-vcam/releases/latest).
2. **Run the installer** and approve the administrator prompt (it is not code-signed yet: if
   SmartScreen warns, *More info → Run anyway*). It installs the app, the **DJI VCam** webcam and
   the firewall rules the camera's video needs.
3. **Prepare the camera**: activate it once with DJI Mimo, then keep it on.
4. **Start DJI VCam and click Connect**: approve the pairing request on the camera screen the first
   time, then pick the Wi-Fi network for the camera and enter its password. The live view appears,
   and apps can pick the **DJI VCam** webcam.
5. **Choose the feed** in the toolbar: *low latency*, or *RTMP* (its address for other apps is under
   *Options → Stream addresses*).

Step by step, with every option and troubleshooting: [docs/installing.md](docs/installing.md). To
build from source instead: [docs/building.md](docs/building.md).

## Documentation

| Document | For |
|---|---|
| [docs/installing.md](docs/installing.md) | Installing and using the app |
| [docs/building.md](docs/building.md) | Building the app and the tools |
| [docs/app-architecture.md](docs/app-architecture.md) | Architecture, milestones, open questions |
| [docs/protocol-notes.md](docs/protocol-notes.md) | The camera protocol: Bluetooth, datalink, live view, findings |
| [docs/camera-controls.md](docs/camera-controls.md) | Camera settings over DUML: every Mimo control, status topics, experiments |
| [docs/tasks.md](docs/tasks.md) | The living task list |
| [CHANGELOG.md](CHANGELOG.md) | What changed in each release |

## How it works

```
                   Bluetooth: pair, Live Streaming mode, join the network, keep-alive
 DJI VCam app  ◄──────────────────────────────────────────────────────────────►  camera
      ▲                                                                            │
      │                     your Wi-Fi network (the camera joins it)               │
      ├── low-latency feed: the camera's live view (UDP 9004)  ◄───────────────────┤
      └── RTMP feed: go2rtc on this computer (RTMP in, RTSP out)  ◄── RTMP push ───┘
   either feed ──► GPU decode ──► preview + the DJI VCam webcam (1080p)
```

- **Bluetooth LE** (DUML frames on `fff4`/`fff5`) pairs with the camera (one on-camera approval),
  switches it to Live Streaming mode and has it join the network (docs/protocol-notes.md 3.13).
  The app keeps the link up with DJI Mimo's keep-alive; the camera leaves the network without it.
- The app finds the camera's address on the network from its Wi-Fi MAC.
- The **low-latency feed** is the datalink DJI Mimo uses for its preview: UDP 9004 with DUML
  commands and the video (H.264 in datagrams of type `0x02`).
- For the **RTMP feed**, the camera pushes RTMP to go2rtc, run by the app; the app plays go2rtc's
  RTSP output, showing each frame as soon as it is decoded.

## Layout

| Path | What |
|---|---|
| `app/` | The desktop app (C++20, Qt 6): `core/` protocol, `ble/` Bluetooth, `media/` GPU decoding and the RTMP player, `gui/`, `cli/`, `tests/` |
| `tools/` | Python research and test tools (the reference implementation) |
| `dji-vcam.sh` | Entry point for the Python tools |
| `docs/` | Guides, architecture, protocol notes, reference lists from the Mimo APK |

## Quick start (developers)

```bash
app/scripts/setup-windows-deps.sh                                  # FFmpeg and go2rtc for Windows, once
app/scripts/build-windows.sh RelWithDebInfo -DDJIVCAM_BUILD_GUI=ON  # app + tests with MSVC
```

Details and all prerequisites: [docs/building.md](docs/building.md).

## TODO

What is still missing (the full list: [docs/tasks.md](docs/tasks.md)):

- **Stop only the RTMP push**: switching from RTMP back to low latency makes the camera leave and
  rejoin the network (15-20 s), the only way known so far to end its push.
- **Always stream RTMP**: the camera's screens only time out while it streams, and both feeds at
  once would switch instantly. To decide once heat and battery are measured.
- **Without a Wi-Fi network**: let this computer start its own hotspot (Windows Mobile Hotspot) for
  the camera, e.g. outdoors.
- **5 GHz networks**: not tested yet.
- **Other DJI cameras**: test the Osmo Action 4 and 6, the Osmo 360 and the Pocket 3; prefer known
  camera models when several DJI devices are around (today the first DJI device heard is used,
  which could be a DJI Mic).
- **Linux**: Bluetooth over BlueZ, a v4l2loopback virtual webcam.
- **Camera controls**: the remaining DJI Mimo settings (timelapse and hyperlapse, slow motion, photo
  options, audio, AE lock, spot metering, HDR) and status (temperature, timecode).
- **Code signing**: the installer is not signed yet, so Windows SmartScreen warns on first run.
- **ARM64**: a build for Windows 11 on ARM (Snapdragon laptops); only x64 is built today.
- **Windows-only tooling**: packaging and the tools wrapper are bash scripts run from WSL; the app
  already builds natively on Windows.
- **Latency**: hand the decoded GPU frames to the preview and the webcam without copies.

## License

MIT, see [LICENSE](LICENSE). Third-party components keep their own licenses: Qt (LGPL-3.0) and
FFmpeg (LGPL-2.1-or-later), both dynamically linked; the virtual camera's media source is adapted
from VCamSample (MIT) and uses the Windows Implementation Libraries (MIT); the RTMP feed's server is
go2rtc (MIT), bundled as a separate program; on Linux the build fetches SimpleBLE (BUSL-1.1) until
our own BlueZ code replaces it. The Windows packages carry the license texts in `licenses/`.

DJI, Osmo and Mimo are trademarks of SZ DJI Technology Co., Ltd. This project is not affiliated
with or endorsed by DJI.
