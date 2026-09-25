# Installing and using DJI VCam

DJI VCam shows the live view of a **DJI Osmo Action 5 Pro** on your computer over Wi-Fi, with about
**135 ms** of delay, and offers it to other apps (OBS, Zoom, Teams, browsers) as the **DJI VCam**
webcam.

> Status: early preview for Windows 11. Linux support is in progress.

## What you need

- Windows 11 with **Bluetooth** (to wake the camera and read its Wi-Fi details). A desktop without
  Bluetooth needs a USB Bluetooth adapter.
- A **DJI Osmo Action 5 Pro** that has been activated with the DJI Mimo app at least once. It is the
  only camera tested so far; the toolbar shows the model the app found and the status bar warns
  when it is another one.
- A way for the computer to reach the camera's own Wi-Fi while keeping your normal internet
  connection. Today that is the **ESP32-S3 USB bridge** (an ESP32-S3-DevKitC-1 N16R8 flashed with
  `firmware/usb-wifi-bridge`, plugged in through its "USB" port). A second USB Wi-Fi adapter will
  also work once that link type lands.

## Install

**Installer (recommended)**: run `dji-vcam-setup-<version>.exe` and approve the administrator
prompt. It installs DJI VCam into `C:\Program Files\DJI VCam`, registers the **DJI VCam** webcam
for all users, lets the camera's video through Windows Firewall and adds a Start-menu entry. To uninstall, use
*Settings → Apps → Installed apps → DJI VCam*; that removes all of it again. Running a newer
installer upgrades in place.

**Portable ZIP**: extract `dji-vcam-<version>-win64.zip` anywhere and run `dji-vcam.exe`. If
Windows Firewall asks, allow access: the video arrives as network traffic from the camera. The
first time the virtual camera is turned on, the app installs the webcam component itself (one
administrator prompt) and registers the webcam for your user account.

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

While the app has no video, the current step is also shown large in the middle of the window.

While streaming, the right side of the status bar shows the resolution, the camera's Wi-Fi channel
(**Wi-Fi: ch 6**, also while disconnected: see *Camera Wi-Fi channel*), frames per second,
bitrate, **delay** (the longest time a frame spent inside the app in the last second, normally
10-20 ms), **loss** (video packets that never arrived), **held** (frames held back after a loss)
and **dup** (packets the camera sent twice), reconnects, and the decoder: **GPU** with the graphics
adapter's name (e.g. `GPU (Intel(R) Arc(TM) Graphics)`, which GPU on a laptop that has two) or
**CPU**.

After the first approval, later connections need no taps. **Options → Connect on startup** makes
the app connect by itself when it starts.

## Using DJI VCam as a webcam (OBS, Zoom, Teams, browsers)

The **DJI VCam** webcam is always listed, like a physical camera, even while the app is closed
(it then shows a dark gray picture). The **Virtual camera** button in the toolbar (on by default)
sends the live view to it. Apps show it as **DJI VCam (Windows Virtual Camera)**: Windows adds the
suffix to every virtual camera.

- **Portable ZIP, first time only**: the app asks to install the camera component. Windows shows
  an administrator prompt; the component is copied to `C:\ProgramData\DJI VCam` and registered
  there, where the Windows camera service can load it. (The installer does this during setup.)
- **OBS**: add a **Video Capture Device** source and pick *DJI VCam (Windows Virtual Camera)*.
- **One app at a time** by default, as with any webcam ("device in use" in a second app). Recent
  Windows 11 builds can share it: *Settings → Bluetooth & devices → Cameras → DJI VCam → Advanced
  camera settings → Allow multiple apps to use camera at the same time*.
- The webcam is always **1280x720 at 30 fps**. A 4:3 live view (960x720) gets black side bars.
- Without video (not connected, camera asleep, app closed) the webcam shows a dark gray picture.
- The status bar shows **Webcam: ready** (the live view goes to the webcam, no app is using it),
  **Webcam: in use**, **Webcam: off** or **Webcam: not installed**.

To remove what the portable app installed, run `dji-vcam-cli --vcam-unregister` from the app
folder, then in an administrator prompt `regsvr32 /u "C:\ProgramData\DJI VCam\djivcam-source.dll"`
and delete that folder. (The installer's uninstaller does all of it by itself.)

## Camera Wi-Fi channel

The camera's own Wi-Fi uses one 2.4 GHz channel, which it keeps. *Options → Camera Wi-Fi channel*
chooses it at the next Connect: **Automatic** (default) lets the ESP32 bridge listen to the networks
around it once per app start and moves the camera to the quietest of channels 1, 6 and 11 when that
is clearly quieter; or pick **1**, **6** or **11** yourself if the video stutters on a busy channel
(a scan hears how loud the neighbours are, not how busy). Moving the channel adds about 3 s to that
Connect.

## Camera settings

The **Camera settings** panel on the right (toolbar: *Camera settings* shows or hides it) works
like the DJI Mimo app while the camera is connected:

- **Start / Stop recording** and **Take photo** (in Photo mode), with the recording time, battery
  and memory card status above the settings.
- **Shooting**: mode, resolution (with its aspect ratio), frame rate, codec. Picking a resolution
  keeps the frame rate when the camera allows it, else takes the closest allowed one.
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

## Lost video

The camera's Wi-Fi occasionally loses a video packet, mostly while the camera moves; the camera does
not send it again, so the picture shows damaged, "smeared" frames until its next keyframe (at most
one second). By default the app keeps showing them, so the picture stays live. **Options → Freeze
the picture after lost video** holds the last intact frame instead (a brief freeze rather than
noise). The status bar counts lost packets (*loss*) and held frames (*held*).

## Snapshots

**Snapshot** in the toolbar saves the current frame of the live view, at the camera's resolution,
as a PNG in `Pictures\DJI VCam`.

## Options

- **Decoder**: *auto* uses your GPU when possible (shown as e.g. `decoder: GPU (Intel(R) Arc(TM)
  Graphics)`; the tooltip names FFmpeg's backend, e.g. d3d11va), *CPU* forces software decoding.
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
| Stuck on "Waiting for the camera network" | Camera Wi-Fi band must be 2.4 GHz; the app re-wakes the camera after 10 s |
| Camera switched off and on | Nothing to do: the app finds and wakes it again and the video returns about 15 s after the camera is on (the app uses Bluetooth only to wake the camera, then hangs up, as DJI Mimo does) |
| "The camera sent no video, connecting again" | Usually recovers by itself; the camera may be in a menu or playback screen |
| Occasional noise | Wi-Fi interference on 2.4 GHz; keep the bridge and camera close and away from routers |
| Video slow or stuttering although the camera is close | A busy neighbouring network on the camera's channel: *Options → Camera Wi-Fi channel* → pick 1, 6 or 11, then Disconnect / Connect (the camera keeps the channel) |
| Seconds of lag, jerky video | If **delay** stays small, the lag builds up on the radio link or in the camera, usually with a high **loss**: move the bridge closer to the camera and away from routers, then Disconnect / Connect |
| Webcam picture is dark gray | The app is not streaming: start it and connect |
| DJI VCam missing in an app | `dji-vcam-cli --list-cameras` lists what apps can see; restart the other app so it lists cameras again |
| "Device in use" / "Timeout starting video source" | Another app has the webcam: close it (or allow multiple apps, see above) |
