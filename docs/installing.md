# Installing and using DJI VCam

DJI VCam shows the live view of a **DJI Osmo Action 5 Pro** on your computer over Wi-Fi, with about
**135 ms** of delay, and (from milestone 3) offers it to other apps as a webcam.

> Status: early preview for Windows 11. Linux support and the virtual webcam are in progress.

## What you need

- Windows 11 with **Bluetooth** (to wake the camera and read its Wi-Fi details).
- A **DJI Osmo Action 5 Pro** that has been activated with the DJI Mimo app at least once.
- A way for the computer to reach the camera's own Wi-Fi while keeping your normal internet
  connection. Today that is the **ESP32-S3 USB bridge** (an ESP32-S3-DevKitC-1 flashed with
  `firmware/usb-wifi-bridge`, plugged in through its "USB" port). A second USB Wi-Fi adapter will
  also work once that link type lands.

## Install

1. Extract `dji-vcam-<version>-win64.zip` anywhere (for example `C:\Program Files\DJI VCam` or
   your Documents folder). A proper installer is coming.
2. Run `dji-vcam.exe`. If Windows Firewall asks, allow access: the video arrives as network
   traffic from the camera.

## Prepare the camera

- In the camera's settings, set the **Wi-Fi band to 2.4 GHz** (the ESP32-S3 has no 5 GHz radio).
- Keep the camera switched on. It only answers Bluetooth while awake.

## First connection

1. Plug in the ESP32-S3 bridge.
2. Click **Connect**.
3. The status bar shows what is happening:

   | Status | Meaning |
   |---|---|
   | Searching for the camera over Bluetooth | Waiting for the camera to be found; switch it on or wake it |
   | **Approve the pairing request on the camera screen (OBSD)** | First time only: tap to approve on the camera |
   | Waking the camera's Wi-Fi | The camera is turning on its Wi-Fi |
   | Configuring the ESP32 bridge | The bridge is told which network to join |
   | Waiting for the camera network | The bridge is joining the camera's Wi-Fi |
   | Connecting to the camera | Starting the video connection |
   | Streaming | Video is flowing; resolution, fps, bitrate and loss are on the right |

After the first approval, later connections need no taps. **Options → Connect on startup** makes
the app connect by itself when it starts.

## Using the video in OBS (until the virtual webcam lands)

Add a **Window Capture** source in OBS and select the DJI VCam window. This adds a little delay; the
virtual webcam (milestone 3) will make DJI VCam appear as a regular camera in OBS, Zoom, Teams,
browsers and other apps.

## Options

- **Decoder**: *auto* uses your GPU when possible (shown as e.g. `d3d11va (GPU)`), *CPU* forces
  software decoding.
- **Wake the camera over Bluetooth**: turn off if you bring the camera's Wi-Fi up another way.
- **Configure the ESP32 USB bridge automatically**: turn off if you connect to the camera's Wi-Fi
  some other way.
- **Forget the paired camera**: the next connection pairs again (approve it on the camera).

## Troubleshooting

| Symptom | What to try |
|---|---|
| Stuck on "Searching for the camera" | Switch the camera on / wake it; move it closer; check that Bluetooth is on |
| Stuck on "Approve the pairing request" | Look at the camera screen and approve; the request expires after about 90 s and is repeated |
| "ESP32 bridge not found on USB" | Use the board's "USB" port (not "UART"); try another cable |
| Stuck on "Waiting for the camera network" | Camera Wi-Fi band must be 2.4 GHz; the app re-wakes the camera after 20 s |
| "No video, reconnecting" | Usually recovers by itself; the camera may be in a menu or playback screen |
| Occasional noise | Wi-Fi interference on 2.4 GHz; keep the bridge and camera close and away from routers |
