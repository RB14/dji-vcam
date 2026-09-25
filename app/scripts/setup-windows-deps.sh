#!/usr/bin/env bash
# Fetches the Windows build dependencies that have no installer:
#   - FFmpeg 8.1 LGPL shared dev build (BtbN) -> %USERPROFILE%\.dji-vcam\deps\ffmpeg
# Qt 6.8.3 comes from aqtinstall (requirements-dev.txt) and MSVC/CMake/Ninja from winget; see
# app/README.md.
set -euo pipefail

FFMPEG_ZIP="ffmpeg-n8.1-latest-win64-lgpl-shared-8.1"
FFMPEG_URL="https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/$FFMPEG_ZIP.zip"

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
