#!/usr/bin/env bash
# Packages the Windows app into binaries/ (git-ignored):
#   dji-vcam-<version>-x64.zip       portable folder
#   dji-vcam-setup-<version>-x64.exe installer (when Inno Setup 6 is installed:
#                                    winget install JRSoftware.InnoSetup --scope user)
# <version> is the full version of the checkout (scripts/version.sh): "0.1.0" from the release tag,
# "0.1.0-dev+g<commit>" otherwise.
#
# Builds the Release configuration with app/scripts/build-windows.sh, then collects the app folder
# (Qt runtime, FFmpeg and Visual C++ runtime DLLs are already deployed next to the exe), the CLI,
# the user guide and third-party license texts. The installer is made from that same folder by
# app/packaging/windows/dji-vcam.iss.
#
# Usage: app/scripts/package-windows.sh
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$APP_DIR/.." && pwd)"
VERSION="$(sed -n 's/^project(dji-vcam VERSION \([0-9.]*\).*/\1/p' "$APP_DIR/CMakeLists.txt")"
VERSION_FULL="$("$APP_DIR/scripts/version.sh")"
NAME="dji-vcam-$VERSION_FULL-x64"

win_env() { powershell.exe -NoProfile -Command "\$env:$1" | tr -d '\r'; }
WIN_ROOT="${DJIVCAM_WIN_ROOT:-$(win_env USERPROFILE)\\.dji-vcam}"  # as in build-windows.sh
BUILD="$(wslpath -u "$WIN_ROOT")/build"
FFMPEG="$(wslpath -u "${FFMPEG_DIR:-$WIN_ROOT\\deps\\ffmpeg}")"
GO2RTC="$(wslpath -u "${GO2RTC_DIR:-$WIN_ROOT\\deps\\rtmp\\go2rtc}")"

"$APP_DIR/scripts/build-windows.sh" Release -DDJIVCAM_BUILD_GUI=ON

OUT="$REPO_DIR/binaries/$NAME"
rm -rf "$OUT"
mkdir -p "$OUT/licenses"
rsync -a --exclude '*.pdb' --exclude '*.ilk' --exclude '*.exp' --exclude '*.lib' "$BUILD/gui/Release/" "$OUT/"
cp "$BUILD/cli/Release/dji-vcam-cli.exe" "$OUT/"
cp "$REPO_DIR/docs/installing.md" "$OUT/README.md"
cp "$FFMPEG/LICENSE.txt" "$OUT/licenses/FFmpeg-LICENSE.txt"
cp "$APP_DIR/vcam/windows/source/LICENSE-VCamSample.txt" "$OUT/licenses/VCamSample-LICENSE.txt"
cp "$BUILD/_deps/wil-src/LICENSE" "$OUT/licenses/WIL-LICENSE.txt"
cp "$GO2RTC/LICENSE" "$OUT/licenses/go2rtc-LICENSE.txt"
[ -f "$OUT/go2rtc.exe" ] || { echo "go2rtc.exe missing: run app/scripts/setup-windows-deps.sh" >&2; exit 1; }
cat > "$OUT/licenses/THIRD-PARTY-NOTICES.txt" <<'EOF'
DJI VCam bundles the following third-party software, dynamically linked where noted.

Qt 6 (https://www.qt.io) - LGPL-3.0, dynamically linked (Qt6*.dll and the plugin folders).
  Source: https://download.qt.io/official_releases/qt/ ; you may replace the Qt DLLs.

FFmpeg 8.1 (https://ffmpeg.org) - LGPL-2.1-or-later build by BtbN
  (https://github.com/BtbN/FFmpeg-Builds), dynamically linked (avcodec, avutil, swscale,
  swresample DLLs). License text: FFmpeg-LICENSE.txt. Source: https://ffmpeg.org/download.html

VCamSample (https://github.com/smourier/VCamSample) - MIT. The virtual camera media source
  (djivcam-source.dll) is adapted from it. License text: VCamSample-LICENSE.txt.

Windows Implementation Libraries (https://github.com/microsoft/wil) - MIT, header-only, compiled
  into djivcam-source.dll. License text: WIL-LICENSE.txt.
go2rtc 1.9.14 (https://github.com/AlexxIT/go2rtc) - MIT. The RTMP server of the RTMP feed
  (go2rtc.exe, run by the app). License text: go2rtc-LICENSE.txt.

DJI, Osmo and Mimo are trademarks of SZ DJI Technology Co., Ltd. This project is not affiliated
with or endorsed by DJI.
EOF

(cd "$REPO_DIR/binaries" && rm -f "$NAME.zip" && zip -qr "$NAME.zip" "$NAME")
echo "Packaged: binaries/$NAME.zip ($(du -h "$REPO_DIR/binaries/$NAME.zip" | cut -f1))"

ISCC=""
for candidate in "$(win_env LOCALAPPDATA)\Programs\Inno Setup 6\ISCC.exe" \
                 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe' 'C:\Program Files\Inno Setup 6\ISCC.exe'; do
    if [ -f "$(wslpath -u "$candidate")" ]; then
        ISCC="$(wslpath -u "$candidate")"
        break
    fi
done
if [ -z "$ISCC" ]; then
    echo "Inno Setup 6 not found: skipping the installer (winget install JRSoftware.InnoSetup --scope user)"
    exit 0
fi
"$ISCC" /Q "/DAppVersion=$VERSION_FULL" "/DAppNumericVersion=$VERSION" "/DSourceDir=$(wslpath -w "$OUT")" "/DOutputDir=$(wslpath -w "$REPO_DIR/binaries")" \
    "$(wslpath -w "$APP_DIR/packaging/windows/dji-vcam.iss")"
SETUP="binaries/dji-vcam-setup-$VERSION_FULL-x64.exe"
echo "Packaged: $SETUP ($(du -h "$REPO_DIR/$SETUP" | cut -f1))"
