#!/usr/bin/env bash
# Builds the app with MSVC on the Windows host, driven from WSL.
#
# MSBuild/cmd.exe do not cope with \\wsl.localhost working directories, so the sources are mirrored
# to a Windows-local folder first and built there with the Visual Studio 2022 generator.
#
# Usage: app/scripts/build-windows.sh [Debug|Release] [extra cmake -D options...]
# Env:   DJIVCAM_WIN_ROOT  Windows-side work folder (default %USERPROFILE%\.dji-vcam). Not under
#                          AppData or Temp: MSBuild's file tracker ignores reads there, so header
#                          changes would not trigger recompiles (mixed class layouts, crashes).
#        QT_DIR             Qt MSVC kit (default %USERPROFILE%\Qt\6.8.3\msvc2022_64)
#        FFMPEG_DIR         FFmpeg shared dev build (default <work folder>\deps\ffmpeg)
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-Debug}"
shift || true

win_env() { powershell.exe -NoProfile -Command "\$env:$1" | tr -d '\r'; }
WIN_ROOT="${DJIVCAM_WIN_ROOT:-$(win_env USERPROFILE)\\.dji-vcam}"
QT_DIR="${QT_DIR:-$(win_env USERPROFILE)\\Qt\\6.8.3\\msvc2022_64}"
FFMPEG_DIR="${FFMPEG_DIR:-$WIN_ROOT\\deps\\ffmpeg}"
SRC_WIN="$WIN_ROOT\\src"
BUILD_WIN="$WIN_ROOT\\build"

# Mirror the sources (no build trees).
mkdir -p "$(wslpath -u "$SRC_WIN")"
rsync -a --delete --exclude 'build*/' "$APP_DIR/" "$(wslpath -u "$SRC_WIN")/"

powershell.exe -NoProfile -Command "
    \$ErrorActionPreference = 'Stop'
    # PATH as currently configured (the one inherited through WSL can predate tool installs)
    \$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
    cmake -S '$SRC_WIN' -B '$BUILD_WIN' -G 'Visual Studio 17 2022' -A x64 \`
        -DCMAKE_PREFIX_PATH='$QT_DIR' -DFFMPEG_DIR='$FFMPEG_DIR' $* | Out-Host
    if (\$LASTEXITCODE) { exit \$LASTEXITCODE }
    cmake --build '$BUILD_WIN' --config $CONFIG --parallel | Out-Host
    if (\$LASTEXITCODE) { exit \$LASTEXITCODE }
    ctest --test-dir '$BUILD_WIN' -C $CONFIG --output-on-failure | Out-Host
    exit \$LASTEXITCODE
" | tr -d '\r'
