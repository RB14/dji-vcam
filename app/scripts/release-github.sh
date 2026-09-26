#!/usr/bin/env bash
# Publishes a GitHub release of the current version (docs/building.md "Versions and releases"):
# tags HEAD as v<version> (unless it already is), builds from it the Windows installer and portable
# ZIP (package-windows.sh), writes their SHA-256 checksums, pushes the tag and creates the release
# with the CHANGELOG.md section of that version as its notes.
#
# Usage: app/scripts/release-github.sh
# Needs: a clean checkout and gh (logged in).
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$APP_DIR/.." && pwd)"
OUT="$REPO_DIR/binaries"
REMOTE="${RELEASE_REMOTE:-github}"
die() { echo "release: $*" >&2; exit 1; }

cd "$REPO_DIR"
VERSION="$(sed -n 's/^project(dji-vcam VERSION \([0-9.]*\).*/\1/p' "$APP_DIR/CMakeLists.txt")"
TAG="v$VERSION"
REPO="$(git remote get-url "$REMOTE" | sed -E 's#^(https://github.com/|git@github.com:)##; s#\.git$##')"

git diff --quiet HEAD -- || die "commit your changes first"
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

(cd "$OUT" && sha256sum "$(basename "$SETUP")" "$(basename "$ZIP")" > SHA256SUMS.txt)
git push "$REMOTE" "$TAG"
gh release create "$TAG" "$SETUP" "$ZIP" "$OUT/SHA256SUMS.txt" --repo "$REPO" --verify-tag \
    --title "DJI VCam $VERSION" --notes "$NOTES

How to install: [docs/installing.md](https://github.com/$REPO/blob/$TAG/docs/installing.md)."
echo "Released: https://github.com/$REPO/releases/tag/$TAG"
