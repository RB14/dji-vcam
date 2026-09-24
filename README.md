# DJI VCam

Use a **DJI Osmo Action 5 Pro** as a wireless, low-latency camera on your computer: in **OBS** and,
through a virtual webcam, in any other app. DJI VCam speaks the protocol the DJI Mimo app uses for
its live preview, instead of the camera's RTMP push.

> Status (2026-09-25): the Windows app connects with one click (Bluetooth wake, bridge setup,
> live view), shows H.264 1280x720 at ~30 fps and ~3.5 Mbit/s, decoded on the GPU, with
> **~135 ms glass-to-glass** latency, and offers it to other apps as the **DJI VCam** webcam. A
> Camera settings panel exposes Mimo's controls (awaiting its first test on the camera); a Windows
> installer is built by `app/scripts/package-windows.sh`. Next: Linux and a USB Wi-Fi adapter as
> the network link. See [docs/app-architecture.md](docs/app-architecture.md) and [docs/tasks.md](docs/tasks.md).
>
> The repository is still called `obs-dji`; it will be renamed to `dji-vcam`.

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
| `obs-dji.sh` | Entry point for the Python tools and bridge flashing |
| `docs/` | Guides, architecture, protocol notes, reference lists from the Mimo APK |

## Quick start (developers)

```bash
app/scripts/setup-windows-deps.sh                                  # FFmpeg for Windows, once
app/scripts/build-windows.sh RelWithDebInfo -DDJIVCAM_BUILD_GUI=ON  # app + tests with MSVC
./obs-dji.sh ota-bridge                                            # update the bridge firmware
./obs-dji.sh live --play                                           # Python reference live view
```

Details and all prerequisites: [docs/building.md](docs/building.md).
