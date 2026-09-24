# Building DJI VCam

The repository has three parts:

| Part | Language / toolchain | Where |
|---|---|---|
| Desktop app (`dji-vcam`) | C++20, CMake, Qt 6.8, FFmpeg, SimpleBLE | `app/` |
| ESP32-S3 USB Wi-Fi bridge firmware | C, ESP-IDF 5.5 | `firmware/usb-wifi-bridge/` |
| Research / test tools | Python 3.12 (Windows) | `tools/`, driven by `obs-dji.sh` |

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
`%LOCALAPPDATA%\dji-vcam\deps\ffmpeg` (so that `include\libavcodec\avcodec.h` exists there).
From WSL, `app/scripts/setup-windows-deps.sh` does this for you.

SimpleBLE and GoogleTest are downloaded by CMake at configure time.

### Build natively (PowerShell)

```powershell
cmake -S app -B build -G "Visual Studio 17 2022" -A x64 `
      -DDJIVCAM_BUILD_GUI=ON `
      -DCMAKE_PREFIX_PATH="$env:USERPROFILE\Qt\6.8.3\msvc2022_64" `
      -DFFMPEG_DIR="$env:LOCALAPPDATA\dji-vcam\deps\ffmpeg"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

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
`%LOCALAPPDATA%\dji-vcam\src` and builds in `%LOCALAPPDATA%\dji-vcam\build`:

```bash
app/scripts/setup-windows-deps.sh                                # FFmpeg, once
app/scripts/build-windows.sh RelWithDebInfo -DDJIVCAM_BUILD_GUI=ON
```

### Testing without the camera

- `dji-vcam --replay FILE` plays a recorded H.264 stream in a loop at 30 fps instead of connecting,
  through the same decoding, preview and virtual camera path. Recordings come from
  `dji-vcam-cli --dump FILE` or `./obs-dji.sh live` (`captures/liveview-*.bin`).
- `dji-vcam-cli --decode-bench FILE [--decoder auto|gpu|cpu]` decodes a recording as fast as
  possible and prints the cost of each per-frame step; the live view needs under 33 ms per frame.
  On the development laptop (Intel iGPU) a real recording takes about 7 ms (D3D11VA or CPU).

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
  `./obs-dji.sh flash-bridge COM3` (the port of the "USB JTAG/serial debug unit").
- **Updates** afterwards, with no buttons: `./obs-dji.sh ota-bridge` (sends the image over the
  bridge's USB console; unconfirmed images roll back automatically).

## Python tools

`./obs-dji.sh` creates a Windows virtual environment in `.venv`, installs `requirements.txt` and
dispatches to the tools (`./obs-dji.sh help`). `./obs-dji.sh test` runs the Python unit tests.
