#!/usr/bin/env bash
# Fetches the Windows build dependencies that have no installer:
#   - FFmpeg 8.1 LGPL shared dev build (BtbN) -> %USERPROFILE%\.dji-vcam\deps\ffmpeg
#   - go2rtc (MIT), the RTMP server the app runs for its RTMP feed -> %USERPROFILE%\.dji-vcam\deps\rtmp\go2rtc
#     (a pinned version, checked against its SHA-256)
# Qt 6.8.3 comes from aqtinstall (requirements-dev.txt) and MSVC/CMake/Ninja from winget; see
# app/README.md.
set -euo pipefail

FFMPEG_ZIP="ffmpeg-n8.1-latest-win64-lgpl-shared-8.1"
FFMPEG_URL="https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/$FFMPEG_ZIP.zip"
GO2RTC_URL="https://github.com/AlexxIT/go2rtc/releases/download/v1.9.14/go2rtc_win64.zip"
GO2RTC_SHA256="dd4167d75cb04abe618855b7c71f8658bd009f60c1a71835d134d2c11c939907"

win_env() { powershell.exe -NoProfile -Command "\$env:$1" | tr -d '\r'; }
DEPS="$(wslpath -u "${DJIVCAM_WIN_ROOT:-$(win_env USERPROFILE)\\.dji-vcam}")/deps"
mkdir -p "$DEPS"

if [ ! -f "$DEPS/ffmpeg/include/libavcodec/avcodec.h" ]; then
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    echo "Downloading $FFMPEG_ZIP..."
    curl -fsSL "$FFMPEG_URL" -o "$tmp/ffmpeg.zip"
    unzip -q "$tmp/ffmpeg.zip" -d "$tmp"
    rm -rf "$DEPS/ffmpeg"
    mv "$tmp/$FFMPEG_ZIP" "$DEPS/ffmpeg"
fi
echo "FFmpeg: $DEPS/ffmpeg ($(ls "$DEPS/ffmpeg/bin" | grep -c '\.dll$') DLLs)"

# fetch_server NAME URL SHA256: unpacks the release into deps/rtmp/NAME once.
fetch_server() {
    local name="$1" url="$2" sha="$3" dir="$DEPS/rtmp/$1"
    if [ -f "$dir/$name.exe" ]; then
        return
    fi
    local tmp
    tmp="$(mktemp -d)"
    echo "Downloading $name..."
    curl -fsSL "$url" -o "$tmp/$name.zip"
    echo "$sha  $tmp/$name.zip" | sha256sum -c --quiet
    mkdir -p "$dir"
    unzip -q -o "$tmp/$name.zip" -d "$dir"
    rm -rf "$tmp"
}
fetch_server go2rtc "$GO2RTC_URL" "$GO2RTC_SHA256"
# Its license text, for the packages' licenses folder (the release zip has none).
[ -f "$DEPS/rtmp/go2rtc/LICENSE" ] || curl -fsSL "https://raw.githubusercontent.com/AlexxIT/go2rtc/v1.9.14/LICENSE" \
    -o "$DEPS/rtmp/go2rtc/LICENSE"
echo "RTMP server: $DEPS/rtmp/go2rtc"
