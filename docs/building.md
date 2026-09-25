# Building DJI VCam

The repository has three parts:

| Part | Language / toolchain | Where |
|---|---|---|
| Desktop app (`dji-vcam`) | C++20, CMake, Qt 6.8, FFmpeg, C++/WinRT (Windows Bluetooth) | `app/` |
| ESP32-S3 USB Wi-Fi bridge firmware | C, ESP-IDF 5.5 | `firmware/usb-wifi-bridge/` |
| Research / test tools | Python 3.12 (Windows) | `tools/`, driven by `dji-vcam.sh` |

## Desktop app on Windows

### Prerequisites (one time)

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.ATL --includeRecommended"
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install Git.Git
winget install Python.Python.3.12
```

Qt 6.8.3 (MSVC 2022, 64-bit) with the SerialPort module, via aqtinstall into your user folder:

```powershell
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtserialport -O $env:USERPROFILE\Qt
```

FFmpeg 8.1 LGPL shared development build (headers, import libraries and DLLs): download
`ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip` from
[BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases) and extract it to
`%USERPROFILE%\.dji-vcam\deps\ffmpeg` (so that `include\libavcodec\avcodec.h` exists there).
From WSL, `app/scripts/setup-windows-deps.sh` does this for you.

GoogleTest (and on Linux SimpleBLE) are downloaded by CMake at configure time. Bluetooth on
Windows uses the Windows SDK's C++/WinRT headers.

### Build natively (PowerShell)

```powershell
cmake -S app -B build -G "Visual Studio 17 2022" -A x64 `
      -DDJIVCAM_BUILD_GUI=ON `
      -DCMAKE_PREFIX_PATH="$env:USERPROFILE\Qt\6.8.3\msvc2022_64" `
      -DFFMPEG_DIR="$env:USERPROFILE\.dji-vcam\deps\ffmpeg"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

> Keep the source and build folders out of `AppData` and `Temp`: MSBuild's file tracker ignores
> reads there, so a change to a header would not recompile the files that include it, and the
> result can mix old and new class layouts (this once crashed the media source).

The runnable app is `build\gui\Release\dji-vcam.exe`: the build copies the Qt runtime
(`windeployqt`), the FFmpeg DLLs and the virtual camera's media source (`djivcam-source.dll`) next
to it. The diagnostic CLI is `build\cli\Release\dji-vcam-cli.exe`.

The virtual camera needs the Windows SDK's Media Foundation headers (Windows 11 SDK) and fetches
Microsoft's WIL headers at configure time. To try it without a camera,
`dji-vcam-cli --vcam-test 30` publishes a moving test pattern to *DJI VCam* for 30 seconds (the
media source must be installed: the app offers to do it, see [installing.md](installing.md)).

While working on the media source itself, a rebuilt DLL does not replace the registered copy in
`%ProgramData%\DJI VCam` (the camera service keeps it loaded). Install it with
`app/scripts/update-vcam-source.ps1 -SourceDll <path>` from an administrator PowerShell; it stops
the camera services, swaps and re-registers the DLL, and starts them again.

### Build from WSL

MSBuild cannot work from `\\wsl.localhost` paths, so the script mirrors `app/` to
`%USERPROFILE%\.dji-vcam\src` and builds in `%USERPROFILE%\.dji-vcam\build`:

```bash
app/scripts/setup-windows-deps.sh                                # FFmpeg, once
app/scripts/build-windows.sh RelWithDebInfo -DDJIVCAM_BUILD_GUI=ON
```

### Packaging

```bash
winget install JRSoftware.InnoSetup --scope user     # once, for the installer (no admin needed)
app/scripts/package-windows.sh
```

This builds the Release configuration and writes to `binaries/` (git-ignored), named with the
full version (see [Versions and releases](#versions-and-releases)):

- `dji-vcam-<version>-x64.zip`: the portable app folder (Qt, FFmpeg and Visual C++ runtime
  DLLs, the virtual camera's media source, the CLI, the user guide and license texts).
- `dji-vcam-setup-<version>-x64.exe`: the installer, made from the same folder by
  `app/packaging/windows/dji-vcam.iss` (skipped when Inno Setup is not installed). It installs
  into Program Files, registers the media source from there, adds a Windows Firewall rule for the
  app and a Start-menu entry, and its uninstaller removes all of it (stopping the Windows camera
  services first, since they keep the media source loaded).

Packaging runs from WSL for now (a bash script); on Windows without WSL, build natively as above
and run the app from the build folder.

### Testing without the camera

- `dji-vcam --replay FILE` plays a recorded H.264 stream in a loop at 30 fps instead of connecting,
  through the same decoding, preview and virtual camera path. Recordings come from
  `dji-vcam-cli --dump FILE` or `./dji-vcam.sh live` (`captures/liveview-*.bin`).
- `dji-vcam-cli --decode-bench FILE [--decoder auto|gpu|cpu]` decodes a recording as fast as
  possible and prints the cost of each per-frame step; the live view needs under 33 ms per frame.
  On the development laptop (Intel iGPU) a real recording takes about 3 ms (D3D11VA or CPU).
- `./dji-vcam.sh fake-camera --video captures/liveview-<time>.bin` stands in for the camera's side
  of the datalink: it streams the recording, answers requests and keeps a small settings state
  with the documented status pushes. Point the app at it with the advanced setting
  `connect/cameraIp` = `127.0.0.1` (registry `HKCU\Software\dji-vcam\dji-vcam\connect`, turn off
  the Bluetooth and bridge options too) or `dji-vcam-cli --camera-ip 127.0.0.1`.
- `dji-vcam-cli --ble-scan 10` lists the Bluetooth LE devices advertising nearby and marks DJI
  cameras (with their model byte).
- `dji-vcam-cli --list-cameras` lists the cameras apps can see (Media Foundation);
  `--vcam-register` (administrator) registers the DJI VCam webcam for all users for good, and
  `--vcam-unregister` removes it; the installer runs both. `--vcam-test 10` sends a test pattern.

### Experimenting with camera commands

With the camera connected, the CLI can send any DUML request over the live datalink and show the
camera's status pushes, which is how new camera controls are tried out before they go into the app:

```bash
dji-vcam-cli --seconds 30 --show-messages --send 01,02,8e,0100
```

`--send receiver,cmd_set,cmd_id[,payload]` (hex, repeatable) is sent once streaming starts and
its reply printed (`--send-early`: right after the live-view trigger, before any video);
`--ble-send` sends one over Bluetooth right after the wake. `--show-messages` prints every message
the camera sends except the replies to the session's own keep-alives (`--show-all-messages`
includes them). `--camera` follows the camera's settings through the app's camera controls and
prints them whenever they change; `--camera-set Stabilization=3` (repeatable, codes from
[camera-controls.md](camera-controls.md)) changes one the way the app's panel does.

Connection experiments: `--reconnect-test 3,10,30` drops the session after streaming, stays
silent for each number of seconds and reports whether video comes back; `--ble-hold` keeps the
Bluetooth link open after the wake (the app hangs up, see protocol-notes.md 3.11);
`--ble-release-test` times how long the camera takes to advertise again after a hang-up;
`--wifi-channel N` moves the camera's access point to 2.4 GHz channel N;
`--no-answer` / `--ble-answer` change whether the camera's own requests are answered on the
datalink / over Bluetooth. The app writes a log to `%LOCALAPPDATA%\dji-vcam\dji-vcam\logs`
(the previous run is kept as `dji-vcam.previous.log`).

## Desktop app on Linux

> Status: the core library and tests are built and tested on Linux (Ubuntu under WSL). The GUI,
> Bluetooth (BlueZ) and the Linux virtual camera have not been tested on a real Linux machine yet.

```bash
sudo apt install build-essential cmake ninja-build pkg-config git \
    qt6-base-dev qt6-serialport-dev libgl-dev \
    libavcodec-dev libavutil-dev libswscale-dev \
    libdbus-1-dev
cmake -S app -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDJIVCAM_BUILD_GUI=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Core library and tests only (any platform)

No Qt, FFmpeg or Bluetooth needed:

```bash
cmake -S app -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core && ctest --test-dir build-core
```

CMake options: `DJIVCAM_BUILD_GUI` (default OFF), `DJIVCAM_WITH_BLE` (default: same as the GUI),
`DJIVCAM_BUILD_TESTS` (default ON).

## ESP32-S3 bridge firmware

Requires ESP-IDF v5.5 (`~/esp/esp-idf`, see Espressif's getting-started guide).

```bash
cd firmware/usb-wifi-bridge
. ~/esp/esp-idf/export.sh
idf.py build
```

- **First flash** (or recovery): hold **BOOT** while plugging the board's "USB" port in, then
  `./dji-vcam.sh flash-bridge COM3` (the port of the "USB JTAG/serial debug unit").
- **Updates** afterwards, with no buttons: `./dji-vcam.sh ota-bridge` (sends the image over the
  bridge's USB console; unconfirmed images roll back automatically).

**On Windows without WSL**: install ESP-IDF v5.5 with Espressif's
[Windows installer](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/windows-setup.html)
and use its "ESP-IDF 5.5 PowerShell":

```powershell
cd firmware\usb-wifi-bridge
idf.py build
idf.py -p COM3 flash          # first flash: hold BOOT while plugging the board's "USB" port in
```

Updates without buttons: `python tools\bridge_ota.py --port COM8` (the bridge's console port; see
[Python tools](#python-tools)). Without building anything, a release's firmware image can also be
flashed from the browser: [installing.md](installing.md#flash-the-esp32-s3-bridge).

## Python tools

`./dji-vcam.sh` creates a Windows virtual environment in `.venv`, installs `requirements.txt` and
dispatches to the tools (`./dji-vcam.sh help`). `./dji-vcam.sh test` runs the Python unit tests.

The tools run on Windows Python either way; without WSL, set up the same environment in PowerShell
and call a tool directly (`dji-vcam.sh` shows which script each command runs):

```powershell
py -3.12 -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt
.venv\Scripts\python tools\bridge_ota.py --port COM8     # e.g. ./dji-vcam.sh ota-bridge
```

## Versions and releases

- **One version number**, [Semantic Versioning](https://semver.org) `MAJOR.MINOR.PATCH`, written
  only in `project(dji-vcam VERSION ...)` in `app/CMakeLists.txt`. While the major version is 0,
  the minor version grows with features and the patch version with fixes.
- **The full version says where a build comes from** (`app/cmake/DjiVcamVersion.cmake`, printed by
  `app/scripts/version.sh`): `0.1.0` for a build of exactly the tag `v0.1.0` with no uncommitted
  changes, `0.1.0-dev+g1a2b3c4` for any other commit (`.dirty` with uncommitted changes). It is in
  the file names of the installer and the ZIP, the Details tab of `dji-vcam.exe`,
  `dji-vcam-cli.exe` and `djivcam-source.dll` (their numeric file version is `0.1.0.0`; builds that
  are not releases are marked pre-release), the app's *Options → About* and log, and
  `dji-vcam-cli --version`. Native builds read git themselves; the WSL-driven Windows build passes
  the version in (`-DDJIVCAM_VERSION=`), since it compiles a copy of the sources without git.
- **Releases are git tags** `vX.Y.Z` with a GitHub release holding the x64 installer, the portable
  ZIP, the bridge firmware image and their SHA-256 checksums. Only x64 is built: the app needs
  Windows 11, which has no 32-bit edition, and the camera service that loads the webcam's media
  source is 64-bit.
- **The bridge firmware has its own version** (`PROJECT_VER` in
  `firmware/usb-wifi-bridge/CMakeLists.txt`, shown by its `version` console command), named in the
  image `dji-vcam-bridge-<version>-esp32s3.bin` and in each release.

To publish a release:

1. Move the changes from *Unreleased* in `CHANGELOG.md` to a section `## X.Y.Z (date)`, set
   `project(dji-vcam VERSION X.Y.Z)`, commit and push.
2. Run `app/scripts/release-github.sh` from a shell where `idf.py` works (after ESP-IDF's
   `export.sh`), with `gh` logged in. It tags HEAD `vX.Y.Z`, builds everything from it and creates
   the release with that changelog section as its notes.
3. Start the next version: set `project()` to the next minor version, so that builds in between are
   named after it (`0.2.0-dev+g...`).
