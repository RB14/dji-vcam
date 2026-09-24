#!/usr/bin/env bash
# Renders the app icon source app/gui/resources/dji-vcam.svg into the multi-size Windows icon
# app/gui/resources/dji-vcam.ico (committed, so Windows builds need no ImageMagick).
#
# Usage: app/scripts/make-icon.sh    (needs ImageMagick 7: sudo apt install imagemagick)
set -euo pipefail

RES="$(cd "$(dirname "${BASH_SOURCE[0]}")/../gui/resources" && pwd)"
magick -background none -density 384 "$RES/dji-vcam.svg" \
    -define icon:auto-resize=256,128,64,48,32,24,16 "$RES/dji-vcam.ico"
echo "Wrote $RES/dji-vcam.ico"
