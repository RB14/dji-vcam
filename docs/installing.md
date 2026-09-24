# Installing and using DJI VCam

DJI VCam shows the live view of a **DJI Osmo Action 5 Pro** on your computer over Wi-Fi, with about
**135 ms** of delay, and offers it to other apps (OBS, Zoom, Teams, browsers) as the **DJI VCam**
webcam.

> Status: early preview for Windows 11. Linux support is in progress.

## What you need

- Windows 11 with **Bluetooth** (to wake the camera and read its Wi-Fi details).
- A **DJI Osmo Action 5 Pro** that has been activated with the DJI Mimo app at least once.
- A way for the computer to reach the camera's own Wi-Fi while keeping your normal internet
  connection. Today that is the **ESP32-S3 USB bridge** (an ESP32-S3-DevKitC-1 flashed with
  `firmware/usb-wifi-bridge`, plugged in through its "USB" port). A second USB Wi-Fi adapter will
  also work once that link type lands.

## Install

**Installer (recommended)**: run `dji-vcam-setup-<version>.exe` and approve the administrator
prompt. It installs DJI VCam into `C:\Program Files\DJI VCam`, registers the **DJI VCam** webcam,
lets the camera's video through Windows Firewall and adds a Start-menu entry. To uninstall, use
*Settings → Apps → Installed apps → DJI VCam*; that removes all of it again. Running a newer
installer upgrades in place.

**Portable ZIP**: extract `dji-vcam-<version>-win64.zip` anywhere and run `dji-vcam.exe`. If
Windows Firewall asks, allow access: the video arrives as network traffic from the camera. The
first time the virtual camera is turned on, the app installs the webcam component itself (one
administrator prompt).

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
   | Streaming | Video is flowing; the figures on the right are explained below |

While streaming, the right side of the status bar shows the resolution, frames per second,
bitrate, **delay** (the longest time a frame spent inside the app in the last second, normally
10-20 ms), **loss** (video packets that never arrived), **recovered** and **dup** (packets the
camera sent late or twice) and reconnects.

After the first approval, later connections need no taps. **Options → Connect on startup** makes
the app connect by itself when it starts.

## Using DJI VCam as a webcam (OBS, Zoom, Teams, browsers)

The **Virtual camera** button in the toolbar (on by default) makes the live view available as a
camera named **DJI VCam** while the app runs. Apps that list cameras through DirectShow (OBS, Zoom,
ffmpeg) show it as **DJI VCam (Windows Virtual Camera)**.

- **Portable ZIP, first time only**: the app asks to install the camera component. Windows shows
  an administrator prompt; the component is copied to `C:\ProgramData\DJI VCam` and registered
  there, where the Windows camera service can load it. (The installer does this during setup.)
- **OBS**: add a **Video Capture Device** source and pick *DJI VCam (Windows Virtual Camera)*.
- The webcam is always **1280x720 at 30 fps**. A 4:3 live view (960x720) gets black side bars.
- Without video (not connected, camera asleep) the webcam shows a dark gray picture.
- The status bar shows **Webcam: ready** (available, no app is using it), **Webcam: in use**,
  **Webcam: off** or **Webcam: not installed**.

To remove the component installed by the portable app, run in an administrator prompt:
`regsvr32 /u "C:\ProgramData\DJI VCam\djivcam-source.dll"`, then delete that folder. (The
installer's uninstaller does this by itself.)

## Camera settings

The **Camera settings** panel on the right (toolbar: *Camera settings* shows or hides it) works
like the DJI Mimo app while the camera is connected:

- **Start / Stop recording** and **Take photo** (in Photo mode), with the recording time, battery
  and memory card status above the settings.
- **Shooting**: mode, recording format (resolution and aspect ratio, frame rate), codec.
- **Image**: stabilization (RockSteady, RockSteady+, HorizonSteady, HorizonBalancing), Daily/Sport,
  FOV.
- **Exposure**: auto or manual; EV, auto ISO limit and anti-flicker in auto; ISO and shutter speed
  in manual.
- **Color**: white balance (auto or Kelvin), color profile (Normal, HLG, D-Log M), texture, noise
  reduction.

The lists only offer what the camera accepts in its current mode and format. Changes are made on
the camera: if it refuses one, the control snaps back and the status bar says why. Settings that
the camera cannot change while recording are disabled during a recording.

> The camera controls are new and have not been tried on a camera yet; report anything that does
> not match what the camera shows.

## Snapshots

**Snapshot** in the toolbar saves the current frame of the live view, at the camera's resolution,
as a PNG in `Pictures\DJI VCam`.

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
| Stuck on "Searching for the camera" | Switch the camera on / wake it; move it closer; check that Bluetooth is on. `dji-vcam-cli --ble-scan 10` (in the app folder) lists what the computer hears; the camera shows up as "DJI" |
| Stuck on "Approve the pairing request" | Look at the camera screen and approve; the request expires after about 90 s and is repeated |
| "ESP32 bridge not found on USB" | Use the board's "USB" port (not "UART"); try another cable |
| Stuck on "Waiting for the camera network" | Camera Wi-Fi band must be 2.4 GHz; the app re-wakes the camera after 20 s |
| "No video, reconnecting" | Usually recovers by itself; the camera may be in a menu or playback screen |
| Occasional noise | Wi-Fi interference on 2.4 GHz; keep the bridge and camera close and away from routers |
| Seconds of lag, jerky video | If **delay** stays small, the lag builds up on the radio link or in the camera, usually with a high **loss**: move the bridge closer to the camera and away from routers, then Disconnect / Connect |
| Webcam picture is dark gray | The app is not streaming: connect first. The webcam only exists while the app runs |
| DJI VCam missing in an app | Check that the status bar says "Webcam: ready"; restart the other app so it lists cameras again |
