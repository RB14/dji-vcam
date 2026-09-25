# DJI VCam

Use a **DJI Osmo Action 5 Pro** as a wireless, low-latency camera on your computer: in **OBS** and,
through a virtual webcam, in any other app. DJI VCam speaks the protocol the DJI Mimo app uses for
its live preview, instead of the camera's RTMP push.

> Status (2026-09-25): the Windows app connects with one click (Bluetooth wake, bridge setup,
> live view), shows H.264 1280x720 at ~30 fps and ~3.5 Mbit/s, decoded on the GPU, with
> **~135 ms glass-to-glass** latency, and offers it to other apps as the **DJI VCam** webcam. A
> Camera settings panel exposes Mimo's controls (verified on the camera); a Windows installer is
> built by `app/scripts/package-windows.sh`. Next: Linux and a USB Wi-Fi adapter as the network
> link. See [docs/app-architecture.md](docs/app-architecture.md) and [docs/tasks.md](docs/tasks.md).

## Requirements

> **Tested only with the DJI Osmo Action 5 Pro.** Other DJI cameras may work or may need changes:
> see [Other DJI cameras](#other-dji-cameras). There are no prebuilt downloads yet: build the app
> (and its installer) from source, see [docs/building.md](docs/building.md).

| What | Why |
|---|---|
| **DJI Osmo Action 5 Pro**, activated once with DJI Mimo, its Wi-Fi band set to **2.4 GHz** | The camera the app is developed and tested with. The ESP32-S3 has no 5 GHz radio |
| **Windows 11** | The webcam uses Windows 11's virtual camera API (Media Foundation `MFCreateVirtualCamera`), which Windows 10 does not have. Linux support is in progress |
| **Bluetooth LE** on the computer | The camera's Wi-Fi is off until an app wakes it over Bluetooth. The same short Bluetooth session pairs the computer with the camera (one approval on the camera screen) and reads the camera's Wi-Fi password; the app hangs up right after, as DJI Mimo does. A desktop without Bluetooth needs a USB Bluetooth adapter (any adapter Windows supports should do; not tested yet) |
| **ESP32-S3 board** flashed with [`firmware/usb-wifi-bridge`](firmware/usb-wifi-bridge/README.md), plugged in through its native "USB" port | Connects the computer to the camera's Wi-Fi, see below. Developed on an ESP32-S3-DevKitC-1 **N16R8** (16 MB flash, 8 MB octal PSRAM); other variants need a change in the firmware configuration |
| A GPU (optional) | Decoding uses the GPU when there is one (D3D11VA), else the CPU |

### Why an ESP32 board?

The camera sends its live view only over its **own Wi-Fi access point**, so the computer has to join
that network. A computer's Wi-Fi card joins one network at a time: joining the camera with it would
cut the computer off the internet (unless it also has Ethernet), which is not what you want while
streaming or in a call. Instead, the ESP32-S3 joins the camera's network and shows up on USB as a
network adapter, so the computer keeps its own Wi-Fi for the internet. The board also strips the
router and DNS settings from the camera's replies, so no internet traffic is ever sent to the
camera. Other ways to reach the camera are on the [TODO list](#todo).

### Other DJI cameras

The app reads the model from the camera's Bluetooth advertisement and shows it in the toolbar
(*Camera: Osmo Action 5 Pro (…)*); it knows the Osmo Action 2, 3, 4, 5 Pro and 6, the Osmo 360 and
the Osmo Pocket 3 and 4, and warns when the camera is not the tested Action 5 Pro. These cameras
belong to the same family: DJI Mimo drives them with the same SDK, and the Pocket 3 streams its
preview over its access point the same way (as the open-source PocketShow shows). So they may work,
or need small changes (a model-specific command, another Bluetooth detail); nobody has tried yet.
Reports are welcome.

## Documentation

| Document | For |
|---|---|
| [docs/installing.md](docs/installing.md) | Installing and using the app |
| [docs/building.md](docs/building.md) | Building the app, the bridge firmware and the tools |
| [docs/app-architecture.md](docs/app-architecture.md) | Architecture, milestones, open questions |
| [docs/protocol-notes.md](docs/protocol-notes.md) | The camera protocol: Bluetooth, datalink, live view, findings |
| [docs/camera-controls.md](docs/camera-controls.md) | Camera settings over DUML: every Mimo control, status topics, experiments |
| [docs/tasks.md](docs/tasks.md) | The living task list |
| [firmware/usb-wifi-bridge/README.md](firmware/usb-wifi-bridge/README.md) | The ESP32-S3 USB Wi-Fi bridge |

## How it works

```
 DJI VCam app (laptop, stays on home Wi-Fi for internet)
   ▲                ▲
   │ Bluetooth      │ USB-C: network adapter + console
   │                ESP32-S3 USB Wi-Fi bridge  ── 2.4 GHz Wi-Fi ──▶  camera's own AP (192.168.2.1)
   └─ pair, wake the camera's Wi-Fi, read its credentials ─────────────┘
```

- **Bluetooth LE** (DUML frames on `fff4`/`fff5`) pairs with the camera (one on-camera approval),
  wakes its Wi-Fi access point and reads the AP credentials.
- The camera's **own AP** carries the Mimo datalink: UDP 9004 with DUML commands and the live-view
  video (H.264 in datagrams of type `0x02`).
- The laptop keeps its own Wi-Fi for the internet, so an **ESP32-S3** joins the camera AP and shows
  up as a USB network adapter. It strips router/DNS options from the camera's DHCP replies so the
  laptop never routes internet traffic through the camera. A second USB Wi-Fi adapter will be an
  alternative.

## Layout

| Path | What |
|---|---|
| `app/` | The desktop app (C++20, Qt 6): `core/` protocol, `ble/` Bluetooth, `media/` GPU decoding, `gui/`, `cli/`, `tests/` |
| `firmware/usb-wifi-bridge/` | ESP-IDF firmware for the ESP32-S3 USB Wi-Fi bridge (OTA-updatable) |
| `tools/` | Python research and test tools (the reference implementation) |
| `dji-vcam.sh` | Entry point for the Python tools and bridge flashing |
| `docs/` | Guides, architecture, protocol notes, reference lists from the Mimo APK |

## Quick start (developers)

```bash
app/scripts/setup-windows-deps.sh                                  # FFmpeg for Windows, once
app/scripts/build-windows.sh RelWithDebInfo -DDJIVCAM_BUILD_GUI=ON  # app + tests with MSVC
./dji-vcam.sh ota-bridge                                            # update the bridge firmware
./dji-vcam.sh live --play                                           # Python reference live view
```

Details and all prerequisites: [docs/building.md](docs/building.md).

## TODO

What is still missing (the full list: [docs/tasks.md](docs/tasks.md)):

- **Reach the camera without the ESP32 board**
  - With a **second USB Wi-Fi adapter**: the app joins the camera's network on it and keeps the
    internet on the computer's own Wi-Fi.
  - With the **computer's own Wi-Fi**: join the camera's network directly. The computer then has no
    Wi-Fi internet while the camera is connected (fine with Ethernet, or when no internet is
    needed), but it needs no extra hardware at all.
- **Bluetooth from the ESP32 board**: the ESP32-S3 has Bluetooth LE too. Waking the camera from the
  board would make Bluetooth on the computer unnecessary (desktops).
- **Other DJI cameras**: test the Osmo Action 4 and 6, the Osmo 360 and the Pocket 3; prefer known
  camera models when several DJI devices are around (today the first DJI device heard is used,
  which could be a DJI Mic).
- **1080p**: the live view is the camera's 1280x720 preview at 30 fps; the camera's RTMP mode
  reaches 1080p.
- **Linux**: Bluetooth over BlueZ, a v4l2loopback virtual webcam, the network link.
- **Camera controls**: the remaining DJI Mimo settings (timelapse and hyperlapse, slow motion, photo
  options, audio, AE lock, spot metering, HDR) and status (temperature, timecode).
- **Releases**: a signed installer and the bridge firmware as downloads.
- **Latency**: hand the decoded GPU frames to the preview and the webcam without copies.

## License

MIT, see [LICENSE](LICENSE). Third-party components keep their own licenses: Qt (LGPL-3.0) and
FFmpeg (LGPL-2.1-or-later), both dynamically linked; the virtual camera's media source is adapted
from VCamSample (MIT) and uses the Windows Implementation Libraries (MIT); on Linux the build
fetches SimpleBLE (BUSL-1.1) until our own BlueZ code replaces it. The Windows packages carry the
license texts in `licenses/`.

DJI, Osmo and Mimo are trademarks of SZ DJI Technology Co., Ltd. This project is not affiliated
with or endorsed by DJI.
