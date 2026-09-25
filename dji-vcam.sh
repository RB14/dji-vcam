#!/usr/bin/env bash
# dji-vcam: entry point for the project's tools.
#
# The Python tools run on *Windows* Python (from WSL): Bluetooth, the serial ports and the ESP32
# USB network adapter all live on the Windows host. The venv at .venv is therefore a Windows venv.
#
# Usage: ./dji-vcam.sh <command> [args]
#   ble scan|creds [--bridge COMx] [-v]   pair with the camera over BLE, read its AP credentials
#   bridge [--port COMx] <console cmd>    talk to the ESP32 USB Wi-Fi bridge (status, scan, wifi ...)
#   live [--seconds N] [-v]               open the camera datalink and try to start the live view
#   fake-camera [--video FILE]            stand-in for the camera's datalink side (app development)
#   capture prepare|start|stop|pull       record a Mimo session on the rooted Android phone
#   flash-bridge <COMx>                   full flash of firmware/usb-wifi-bridge (board in download mode)
#   ota-bridge [--port COMx]              update the running bridge over its USB console (no buttons)
#   test                                  run the unit tests
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venv"
PYTHON="$VENV_DIR/Scripts/python.exe"
WIN_PYTHON="${WIN_PYTHON:-python.exe}"
REQUIREMENTS="$SCRIPT_DIR/requirements.txt"
STAMP="$VENV_DIR/.requirements.sha256"

win() { wslpath -w "$1"; }

# Create the Windows venv if missing
if [ ! -f "$PYTHON" ]; then
    echo "Creating Windows virtual environment..."
    "$WIN_PYTHON" -m venv "$(win "$VENV_DIR")"
fi
# Windows-created executables lack the exec bit when seen from WSL
chmod +x "$VENV_DIR"/Scripts/*.exe 2>/dev/null || true

# Install / update deps when requirements.txt changed
wanted="$(sha256sum "$REQUIREMENTS" | cut -d' ' -f1)"
if [ "$(cat "$STAMP" 2>/dev/null)" != "$wanted" ]; then
    echo "Installing dependencies..."
    "$PYTHON" -m pip install -q --upgrade pip
    "$PYTHON" -m pip install -q -r "$(win "$REQUIREMENTS")"
    chmod +x "$VENV_DIR"/Scripts/*.exe 2>/dev/null || true
    echo "$wanted" > "$STAMP"
fi

command="${1:-help}"
shift || true
case "$command" in
    ble)
        "$PYTHON" "$(win "$SCRIPT_DIR/tools/dji_ble.py")" "$@"
        ;;
    bridge)
        "$PYTHON" "$(win "$SCRIPT_DIR/tools/bridge_console.py")" "$@"
        ;;
    live)
        "$PYTHON" "$(win "$SCRIPT_DIR/tools/dji_liveview.py")" "$@"
        ;;
    fake-camera)
        "$PYTHON" "$(win "$SCRIPT_DIR/tools/fake_camera.py")" "$@"
        ;;
    capture)
        "$SCRIPT_DIR/tools/phone_capture.sh" "$@"
        ;;
    flash-bridge)
        # Full flash with esptool (board in download mode: hold BOOT while plugging it in, or use the
        # board's UART port). Offsets and files come from ESP-IDF's own build/flash_args.
        port="${1:?usage: flash-bridge <COMx>}"
        build="$SCRIPT_DIR/firmware/usb-wifi-bridge/build"
        read -r -a opts < "$build/flash_args"
        images=()
        while read -r offset file; do
            images+=("$offset" "$(win "$build/$file")")
        done < <(tail -n +2 "$build/flash_args")
        "$PYTHON" -m esptool --chip esp32s3 --port "$port" -b 921600 \
            --before default-reset --after watchdog-reset write-flash "${opts[@]//_/-}" "${images[@]}"
        ;;
    ota-bridge)
        # Firmware update over the running bridge's USB console; no buttons needed.
        "$PYTHON" "$(win "$SCRIPT_DIR/tools/bridge_ota.py")" "$@"
        ;;
    test)
        cd "$SCRIPT_DIR/tools" && "$PYTHON" -m unittest -v
        ;;
    *)
        sed -n '2,16p' "$0"
        ;;
esac
