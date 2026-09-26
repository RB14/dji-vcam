# Installing and using DJI VCam

DJI VCam shows the live view of a **DJI Osmo Action 5 Pro** on your computer over your Wi-Fi
network, in 1080p with about **135 ms** of delay, and offers it to other apps (OBS, Zoom, Teams,
browsers) as the **DJI VCam** webcam. It can also take the camera's RTMP stream instead.

> Status: early preview for Windows 11. Linux support is in progress.

## What you need

- Windows 11 with **Bluetooth**. The app pairs with the camera over Bluetooth and tells it which
  network to join; the link stays up while connected. A desktop without Bluetooth needs a USB
  Bluetooth adapter.
- A **DJI Osmo Action 5 Pro** that has been activated with the DJI Mimo app at least once. It is the
  only camera tested so far; the toolbar shows the model the app found and the status bar warns
  when it is another one.
- A **Wi-Fi network the camera and this computer share**: the camera joins it, and the computer is
  on it over Wi-Fi or Ethernet. It must not be a guest network that keeps its devices apart. 2.4 GHz
  networks work; 5 GHz is not tested yet. Hidden networks work too.

## Download

Everything is on the [Releases page](https://github.com/RB14/dji-vcam/releases/latest):
`dji-vcam-setup-<version>-x64.exe` (installer), `dji-vcam-<version>-x64.zip` (portable) and
`SHA256SUMS.txt`.

## Install

**Installer (recommended)**: run `dji-vcam-setup-<version>-x64.exe` and approve the administrator
prompt. The installer is not code-signed yet, so Windows SmartScreen may say *"Windows protected
your PC"*: click **More info → Run anyway**. It installs DJI VCam into `C:\Program Files\DJI VCam`,
registers the **DJI VCam** webcam for all users, lets the camera's video and RTMP stream through
Windows Firewall, and adds a Start-menu entry. To uninstall, use *Settings → Apps → Installed apps
→ DJI VCam*; that removes all of it again. Running a newer installer upgrades in place.

**Portable ZIP**: extract `dji-vcam-<version>-x64.zip` anywhere and run `dji-vcam.exe`. If
Windows Firewall asks (for `dji-vcam.exe`, and for `go2rtc.exe` with the RTMP feed), allow access:
the video arrives as network traffic from the camera. The first time the virtual camera is turned
on, the app installs the webcam component itself (one administrator prompt) and registers the
webcam for your user account.

## First connection

1. Switch the camera on (it only answers Bluetooth while awake).
2. Click **Connect**.
3. The first time, approve the pairing request on the camera screen.
4. The first time, the **Camera Wi-Fi network** dialog lists the networks the camera hears, this
   computer's own network first. Pick it (or type the name of a hidden network), enter its password
   and click **Join**. The app keeps the password, encrypted for your Windows account; *Options →
   Camera Wi-Fi network* changes it later.
5. The camera joins the network (its screen says "Preparing to live stream"), the app finds it there,
   starts the camera's RTMP stream and then the video.

The status bar shows what is happening:

| Status | Meaning |
|---|---|
| Searching for the camera over Bluetooth | Waiting for the camera to be found; switch it on or wake it |
| **Approve the pairing request on the camera screen (OBSD)** | First time only: tap to approve on the camera |
| Switching the camera to Live Streaming mode | The mode in which the camera can join a network |
| Asking the camera which Wi-Fi networks it hears | Filling the network dialog |
| **Choose the Wi-Fi network for the camera** | The network dialog is waiting for you |
| The camera is joining *network* | It connects to your network (up to about 10 s) |
| The camera is on *network*: looking for it there | The app finds the camera's address on the network |
| Starting the camera's RTMP stream | The camera connects to the app's RTMP server (a few seconds) |
| Connecting to the camera | Starting the video connection |
| Streaming | Video is flowing; the figures on the right are explained below |

While the app has no video, the current step is also shown large in the middle of the window.

While streaming, the right side of the status bar shows the resolution, the network the camera is
on and its address there (**Wi-Fi: HomeNet · 192.168.1.23**), frames per second, bitrate, **delay**
(the longest time a frame spent inside the app in the last second, normally a few ms), **loss**
(video packets that never arrived), **held** (frames held back after a loss) and **dup** (packets
the camera sent twice), reconnects, and the decoder: **GPU** with the graphics adapter's name (e.g.
`GPU (Intel(R) Arc(TM) Graphics)`, which GPU on a laptop that has two) or **CPU**.

After the first time, Connect needs no taps and no choice. **Options → Connect on startup** makes
the app connect by itself when it starts. **Disconnect** returns the camera to its usual Video mode
(it leaves your network).

## The two feeds

The toolbar's **Feed** choice selects what the app plays:

- **Low latency** (default): the camera's live view, the one DJI Mimo shows, about 0.15 s behind
  reality, in 1920x1080.
- **RTMP**: the camera pushes an RTMP stream to a small RTMP server that comes with the app
  (go2rtc), and the app plays it, about 0.4 s behind (the camera buffers its livestream). *Options →
  RTMP quality* chooses 1080p at 6 Mbit/s or 720p at 4 Mbit/s. Other apps can open the same stream:
  *Options → Stream addresses* shows its addresses, e.g. for an OBS *Media Source*
  (`rtsp://127.0.0.1:8554/live`).

**Options → Keep the RTMP stream running alongside the live view** (on by default) runs both at
once: the camera pushes its RTMP stream, and the app keeps the live view connection too. The Feed
choice then only picks which picture you see, and switches at once.

| | Low latency only | RTMP only | Both (the option, default) |
|---|---|---|---|
| Delay | ~0.15 s | ~0.4 s | ~0.15 s or ~0.4 s, as the Feed picks |
| Camera screens | **stay on** ("Preparing to live stream") | turn off after their timeout | turn off after their timeout |
| Camera settings | all of them | only stabilization, scene, FOV, auto ISO limit (over Bluetooth) | all of them, on both feeds |
| Switching the Feed | the camera rejoins the network, 15-20 s | the camera rejoins the network, 15-20 s | instant |
| Other apps (OBS, VLC) | through the DJI VCam webcam | also the RTMP stream's addresses | both |
| Cost | ~3.5 Mbit/s of Wi-Fi | ~6 Mbit/s (4 at 720p) | both: ~10 Mbit/s, more heat and battery (not measured yet) |
| Lost video | occasional damaged frames (UDP, see below) | none (TCP) | as the Feed picks |

With the option off, the app runs one feed at a time. A switch makes the camera leave and rejoin
the network: the camera only ends its RTMP stream that way, and ignores the RTMP settings while its
live view runs. Changing the option while connected also rejoins.

## Using DJI VCam as a webcam (OBS, Zoom, Teams, browsers)

The **DJI VCam** webcam is always listed, like a physical camera, even while the app is closed
(it then shows a dark gray picture). The **Virtual camera** button in the toolbar (on by default)
sends the video (of either feed) to it. Apps show it as **DJI VCam (Windows Virtual Camera)**:
Windows adds the suffix to every virtual camera.

- **Portable ZIP, first time only**: the app asks to install the camera component. Windows shows
  an administrator prompt; the component is copied to `C:\ProgramData\DJI VCam` and registered
  there, where the Windows camera service can load it. (The installer does this during setup.)
- **OBS**: add a **Video Capture Device** source and pick *DJI VCam (Windows Virtual Camera)*.
- **One app at a time** by default, as with any webcam ("device in use" in a second app). Recent
  Windows 11 builds can share it: *Settings → Bluetooth & devices → Cameras → DJI VCam → Advanced
  camera settings → Allow multiple apps to use camera at the same time*.
- The webcam is always **1920x1080 at 30 fps**; smaller video (the 720p RTMP feed) is scaled up, a
  4:3 picture gets black side bars.
- Without video (not connected, camera asleep, app closed) the webcam shows a dark gray picture.
- The status bar shows **Webcam: ready** (the video goes to the webcam, no app is using it),
  **Webcam: in use**, **Webcam: off** or **Webcam: not installed**.

To remove what the portable app installed, run `dji-vcam-cli --vcam-unregister` from the app
folder, then in an administrator prompt `regsvr32 /u "C:\ProgramData\DJI VCam\djivcam-source.dll"`
and delete that folder. (The installer's uninstaller does all of it by itself.)

## Camera settings

The **Camera settings** panel on the right (toolbar: *Camera settings* shows or hides it) works
like the DJI Mimo app:

- **Image**: stabilization (RockSteady, RockSteady+, HorizonSteady, HorizonBalancing), Daily/Sport,
  FOV.
- **Exposure**: auto or manual; EV, auto ISO limit and anti-flicker in auto; ISO and shutter speed
  in manual.
- **Color**: white balance (auto or Kelvin), color profile (Normal, HLG, D-Log M), texture, noise
  reduction.
- The battery and memory card status above the settings.

While connected, the camera is in its **Live Streaming mode**: its shooting mode, recording format
and codec do not apply (a change of mode would disconnect it), and recording in this mode is not
supported yet, so those controls are hidden. The lists only offer what the camera accepts; if it
refuses a change, the control snaps back and the status bar says why.

With the RTMP stream alone (the option off), the settings go over Bluetooth, and there the camera
only takes stabilization, Daily/Sport, FOV and the auto ISO limit: exposure and color are shown
but greyed out. Change them on the low-latency feed (the camera keeps them), or turn the option on.

## Lost video

The Wi-Fi occasionally loses a video packet of the low-latency feed; the camera does not send it
again, so the picture shows damaged, "smeared" frames until its next keyframe (at most one second).
By default the app keeps showing them, so the picture stays live. **Options → Freeze the picture
after lost video** holds the last intact frame instead (a brief freeze rather than noise). The
status bar counts lost packets (*loss*) and held frames (*held*). The RTMP feed never loses packets
(it runs over TCP); its cost is the extra delay.

## Snapshots

**Snapshot** in the toolbar saves the current frame, at the camera's resolution, as a PNG in
`Pictures\DJI VCam`.

## Options

- **Feed** and **Decoder** (toolbar): the feed, see above; *auto* decodes on your GPU when possible
  (shown as e.g. `decoder: GPU (Intel(R) Arc(TM) Graphics)`; the tooltip names FFmpeg's backend,
  e.g. d3d11va), *CPU* forces software decoding.
- **Connect on startup**, **Freeze the picture after lost video**: see above.
- **Camera Wi-Fi network**: the network the camera joins, and its password. While connected, a new
  choice moves the camera to it.
- **RTMP quality**, **Keep the RTMP stream running alongside the live view**, **Stream addresses**:
  the RTMP feed and both feeds at once, see above. Changing the quality or the option while
  connected makes the camera rejoin the network.
- **Forget the paired camera**: the next connection pairs again (approve it on the camera).
- **About DJI VCam**: the version (e.g. `0.2.0`); please include it, and the log from
  `%LOCALAPPDATA%\dji-vcam\dji-vcam\logs`, when reporting a problem on
  [GitHub](https://github.com/RB14/dji-vcam/issues).

## Troubleshooting

| Symptom | What to try |
|---|---|
| Stuck on "Searching for the camera" | Switch the camera on / wake it; move it closer; check that Bluetooth is on. `dji-vcam-cli --ble-scan 10` (in the app folder) lists what the computer hears; the camera shows up as "DJI" |
| Stuck on "Approve the pairing request" | Look at the camera screen and approve; the request expires after about 90 s and is repeated |
| "The camera could not join *network*" | Check the password in the dialog; the network must reach the camera (2.4 GHz, near enough) |
| "The camera is on *network*, but this computer does not see it there" | The computer must be on the same network, and it must not be a guest network that keeps devices apart (router setting "client/AP isolation") |
| The camera leaves the network after a while | The Bluetooth link was lost (out of range, Bluetooth turned off): the app reconnects and the camera rejoins by itself |
| Camera switched off and on | Nothing to do: the app finds it, puts it back on the network and the video returns |
| Stuck on "Waiting for the camera's RTMP stream" | Windows Firewall must let `go2rtc.exe` in on TCP 1935 (the installer does; with the ZIP, allow it when asked). *Options → Stream addresses* shows where the camera pushes |
| "The camera sent no video, connecting again" | Usually recovers by itself |
| Occasional noise on the low-latency feed | Wi-Fi interference: keep the camera closer to the router, or use the RTMP feed |
| Seconds of lag, jerky video | If **delay** stays small, the lag builds up on the radio link or in the camera, usually with a high **loss**: move the camera closer to the router, then Disconnect / Connect |
| Webcam picture is dark gray | The app is not streaming: start it and connect |
| DJI VCam missing in an app | `dji-vcam-cli --list-cameras` lists what apps can see; restart the other app so it lists cameras again |
| "Device in use" / "Timeout starting video source" | Another app has the webcam: close it (or allow multiple apps, see above) |
