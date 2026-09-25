#!/usr/bin/env bash
# Publishes a GitHub release of the current version (docs/building.md "Versions and releases"):
# tags HEAD as v<version> (unless it already is), builds from it the Windows installer and portable
# ZIP (package-windows.sh) and the ESP32-S3 bridge firmware as one image to flash at 0x0, writes
# their SHA-256 checksums, pushes the tag and creates the release with the CHANGELOG.md section of
# that version as its notes.
#
# Usage: app/scripts/release-github.sh
# Needs: a clean checkout, gh (logged in), and idf.py on the PATH (source ESP-IDF's export.sh).
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$APP_DIR/.." && pwd)"
FW_DIR="$REPO_DIR/firmware/usb-wifi-bridge"
OUT="$REPO_DIR/binaries"
REMOTE="${RELEASE_REMOTE:-github}"
die() { echo "release: $*" >&2; exit 1; }

cd "$REPO_DIR"
VERSION="$(sed -n 's/^project(dji-vcam VERSION \([0-9.]*\).*/\1/p' "$APP_DIR/CMakeLists.txt")"
FW_VERSION="$(sed -n 's/^set(PROJECT_VER "\(.*\)")/\1/p' "$FW_DIR/CMakeLists.txt")"
TAG="v$VERSION"
REPO="$(git remote get-url "$REMOTE" | sed -E 's#^(https://github.com/|git@github.com:)##; s#\.git$##')"

git diff --quiet HEAD -- || die "commit your changes first"
command -v idf.py >/dev/null || die "idf.py not found: source ESP-IDF's export.sh first"
NOTES="$(awk -v v="$VERSION" 'index($0, "## " v " ") == 1 {on = 1; next} /^## / && on {exit} on' CHANGELOG.md)"
[ -n "$(echo "$NOTES" | tr -d '[:space:]')" ] || die "CHANGELOG.md has no section for $VERSION"
gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1 && die "$REPO already has a release $TAG"

if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    [ "$(git rev-list -n 1 "$TAG")" = "$(git rev-parse HEAD)" ] || die "$TAG already tags another commit"
else
    git tag -a "$TAG" -m "DJI VCam $VERSION"
fi
[ "$("$APP_DIR/scripts/version.sh")" = "$VERSION" ] || die "the build would not be version $VERSION"

"$APP_DIR/scripts/package-windows.sh"
SETUP="$OUT/dji-vcam-setup-$VERSION-x64.exe"
ZIP="$OUT/dji-vcam-$VERSION-x64.zip"
[ -f "$SETUP" ] || die "no installer: is Inno Setup 6 installed?"

# The bridge firmware as one image (bootloader, partition table, OTA data, app) to flash at 0x0.
idf.py -C "$FW_DIR" build
FIRMWARE="$OUT/dji-vcam-bridge-$FW_VERSION-esp32s3.bin"
(cd "$FW_DIR/build" && python -m esptool --chip esp32s3 merge_bin -o "$FIRMWARE" @flash_args)

(cd "$OUT" && sha256sum "$(basename "$SETUP")" "$(basename "$ZIP")" "$(basename "$FIRMWARE")" > SHA256SUMS.txt)
git push "$REMOTE" "$TAG"
gh release create "$TAG" "$SETUP" "$ZIP" "$FIRMWARE" "$OUT/SHA256SUMS.txt" --repo "$REPO" --verify-tag \
    --title "DJI VCam $VERSION" --notes "$NOTES

Bridge firmware: $FW_VERSION. How to install: [docs/installing.md](https://github.com/$REPO/blob/$TAG/docs/installing.md)."
echo "Released: https://github.com/$REPO/releases/tag/$TAG"
