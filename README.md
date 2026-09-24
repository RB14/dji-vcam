# obs-dji

Use a **DJI Osmo Action 5 Pro** as a wireless, low-latency video source in **OBS**, by speaking the
same protocol the DJI Mimo app uses for its live preview instead of the camera's RTMP push.

> Status: working proof of concept (2026-09-24). The Action 5 Pro live view streams through the
> ESP32-S3 bridge into a player on the laptop: H.264 High@3.2, 1280x720, ~30 fps, ~3.5 Mbit/s,
> **~135 ms glass-to-glass** (measured against an on-screen millisecond clock), no packet loss.
> The protocol is reverse engineered from open-source projects plus our own tests; see
> [docs/protocol-notes.md](docs/protocol-notes.md).

## How it works

```
 OBS (laptop, stays on home Wi-Fi for internet)
   ▲
   │ USB-C: NCM network adapter + CDC console
 ESP32-S3 "USB Wi-Fi bridge"  ── 2.4 GHz Wi-Fi ──▶  camera's own AP (192.168.2.1)
                                                       │
 laptop Bluetooth ─────── BLE: pair, wake AP, read SSID/password ──┘
```

- **BLE** (`fff4` notify / `fff5` write, DUML frames) pairs with the camera, wakes its Wi-Fi AP and
  hands out the AP's SSID and passphrase. The first pairing needs a tap on the camera screen.
- The camera's **own AP** carries the Mimo datalink: UDP 9004 (after a TCP 7001 "poke") with DUML
  commands and, for live view, video packets (UDP type `0x02`, ~4 Mbps, likely 720p).
- The laptop must keep its own Wi-Fi for the internet, so an **ESP32-S3** joins the camera AP and
  shows up on the laptop as a USB network adapter. It scrubs the router/DNS options from the camera's
  DHCP replies, so the laptop never routes internet traffic through the camera.

Constraints worth knowing: the ESP32-S3 is 2.4 GHz only (set the camera's Wi-Fi band to 2.4 GHz), and
its Full-Speed USB tops out around 6–9 Mbps in practice, enough for the ~4 Mbps preview stream.

## Layout

| path | what |
|---|---|
| `obs-dji.sh` | entry point for all tools (creates the Windows venv, installs deps) |
| `tools/duml.py` | DUML framing + CRCs (unit tested against real Mimo frames) |
| `tools/dji_ble.py` | BLE pairing and AP credential retrieval |
| `tools/bridge_console.py` | talk to the ESP32 bridge's serial console |
| `tools/phone_capture.sh` | record a Mimo session (tcpdump + BT HCI snoop) on a rooted Android phone |
| `firmware/usb-wifi-bridge/` | ESP-IDF firmware for the ESP32-S3 USB Wi-Fi bridge |
| `docs/protocol-notes.md` | protocol reference with source citations and open questions |

## Environment

Developed in WSL2 (mirrored networking) on Windows 11. The Python tools run on **Windows Python**
because Bluetooth, serial ports and the bridge's network adapter live on the Windows host; `.venv`
is therefore a Windows venv, driven from WSL by `obs-dji.sh`.

- Firmware: ESP-IDF v5.5 in WSL (`idf.py build` inside `firmware/usb-wifi-bridge`).
- Phone tooling: `adb` (Windows build) talking to a rooted Android phone with DJI Mimo installed.

## Usage

```bash
./obs-dji.sh test                         # unit tests
./obs-dji.sh ble scan                     # find the camera over BLE
./obs-dji.sh ble creds --bridge COM8 -v   # pair, read AP credentials, hand them to the bridge
./obs-dji.sh bridge --port COM8 status    # bridge link state and traffic counters
./obs-dji.sh capture start|stop|pull      # reference capture of a Mimo session on the phone
./obs-dji.sh live --play                  # stream the camera's live view into a low-latency window
./obs-dji.sh ota-bridge                   # update the bridge firmware over USB (no buttons)
./obs-dji.sh flash-bridge COM3            # full flash (board in download mode)
```

## Roadmap

1. ~~Proof of concept: laptop pulls the live view through the ESP32 bridge and plays it.~~ Done.
2. OBS integration: a native source plugin that runs the datalink and decodes frames itself.
3. Robustness: reconnects, camera sleep/wake, one-command start (BLE wake + datalink).
4. Smart bridge: the ESP32 runs BLE + the DJI session itself and streams raw video over USB.
5. Preview quality: look for a bitrate/resolution parameter (e.g. in the DJI Mimo APK).
