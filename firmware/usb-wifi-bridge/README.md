# USB Wi-Fi bridge (ESP32-S3)

Turns an ESP32-S3 (developed on an ESP32-S3-DevKitC-1 N16R8) into a USB network adapter that joins
the DJI camera's 2.4 GHz access point, so the laptop can reach the camera (192.168.2.1) while its own
Wi-Fi stays on the home network. Based on ESP-IDF's `tusb_ncm` example.

Plug the board in through its **"USB"** port (native USB, GPIO19/20), not the "UART" port.

## What it exposes

- **USB NCM network adapter.** A layer-2 bridge: it reuses the Wi-Fi station MAC, so the laptop gets
  its DHCP lease straight from the camera. The adapter reports "cable unplugged" until the station is
  associated, so Windows re-runs DHCP on every (re)join.
- **USB CDC-ACM console** with all logs and a few commands:

  | command | effect |
  |---|---|
  | `status` | link, filter, traffic counters, free heap |
  | `scan` | list nearby 2.4 GHz networks |
  | `wifi <ssid> <password>` | store credentials in NVS and connect (quote SSIDs with spaces) |
  | `forget` | erase credentials, disconnect |
  | `rejoin` | leave the camera's network and join it again |
  | `filter on\|off` | DHCP/RA scrubbing (default on) |
  | `version` | firmware version, build time, running partition and its OTA state |
  | `ota <size> <sha256>` | receive a firmware image (driven by `tools/bridge_ota.py`) |
  | `reboot` | restart |

## Safety filter

Frames from the camera to the laptop pass through `net_filter.c`: DHCP router (3), DNS (6) and
classless route (121/249) options are blanked, and IPv6 router advertisements are dropped. The laptop
only learns a route to the camera subnet and never a default gateway through this adapter.

## Throughput and latency

- Wi-Fi power save is disabled (`WIFI_PS_NONE`); modem sleep would hold downlink frames until the next
  beacon.
- Full-Speed USB is the bottleneck (the camera's live view is ~2.7 Mbit/s with keyframe bursts). The
  Wi-Fi RX callback only queues frames; a pump in the TinyUSB task packs them into 6 x 3.2 KB NCM
  transfer blocks, so bursts are buffered instead of dropped. `status` shows drops and the queue's
  peak. Queued frames keep their Wi-Fi driver RX buffer, so the driver has 256 of them, in the
  board's PSRAM (firmware 0.4.0): with 64, keyframe bursts made it discard frames silently.
- `rejoin` (firmware 0.4.1) leaves the camera's network with a clean deauthentication and joins it
  again (diagnostics). `help` lists every command (0.4.0 cut the list short).

## Build and flash

```bash
. ~/esp/esp-idf/export.sh            # ESP-IDF v5.5
idf.py build
../../obs-dji.sh ota-bridge          # update the running bridge over its USB console: no buttons
../../obs-dji.sh flash-bridge COM3   # full esptool flash; board in download mode (hold BOOT while
                                     # plugging it in, or use the board's "UART" port)
```

## Firmware updates (OTA) and rollback

The partition table has a factory slot and two OTA slots. `ota-bridge` streams the new image over
the CDC console; the firmware checks its SHA-256, writes it to the next slot and reboots into it.
The new image runs "pending verification" until its console handles a command (`bridge_ota.py` sends
`version`). If that does not happen within 90 s, it restarts and the bootloader rolls back to the
previous image, so a broken build cannot lock out further remote updates. A full esptool flash is
only needed for bootloader or partition-table changes, or to recover.

Rebooting into the ROM download mode from the running firmware (switching the USB PHY back from
TinyUSB to USB-Serial-JTAG) left the board unenumerated until a power cycle, which is why updates go
through OTA instead.
