"""Update the ESP32-S3 USB Wi-Fi bridge firmware over its USB console (no buttons, no esptool).

Protocol: send `ota <size> <sha256>`, wait for `ota: ready`, stream the raw image, wait for
`ota: ok, rebooting`. After the reboot the new image runs unconfirmed; this tool reconnects and sends
`version`, which confirms it. An image that never gets confirmed rolls back by itself after 90 s.

    bridge_ota.py --port COM8 [--image firmware/usb-wifi-bridge/build/usb_wifi_bridge.bin]
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import time
from pathlib import Path

import serial

DEFAULT_IMAGE = Path(__file__).resolve().parent.parent / "firmware/usb-wifi-bridge/build/usb_wifi_bridge.bin"
CHUNK = 4096


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def open_console(port_name: str, timeout: float) -> serial.Serial:
    """Opens the console, retrying while the device (re)enumerates."""
    deadline = time.monotonic() + timeout
    while True:
        try:
            port = serial.Serial(port_name, 115200, timeout=0.2)
            port.dtr = True
            time.sleep(0.3)
            port.reset_input_buffer()
            return port
        except serial.SerialException:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.5)


def wait_for(port: serial.Serial, prefix: str, timeout: float) -> str:
    """Returns the first console line starting with `prefix`; raises on `ota: error`."""
    deadline = time.monotonic() + timeout
    buffer = b""
    while time.monotonic() < deadline:
        buffer += port.read(port.in_waiting or 1)
        while b"\n" in buffer:
            raw, buffer = buffer.split(b"\n", 1)
            line = raw.decode(errors="replace").strip()
            if line and not line.startswith(prefix):
                log(f"  device: {line}")
            if line.startswith("ota: error"):
                raise RuntimeError(line)
            if line.startswith(prefix):
                return line
    raise TimeoutError(f"no '{prefix}' within {timeout:.0f}s")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--image", type=Path, default=DEFAULT_IMAGE)
    parser.add_argument("--pace", type=float, default=0.02,
                        help="seconds to pause after each 4 KB chunk (unpaced transfers corrupt past ~512 KB)")
    args = parser.parse_args()

    image = args.image.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    log(f"image {args.image.name}: {len(image)} bytes, sha256 {digest[:16]}...")

    with open_console(args.port, timeout=5) as port:
        port.write(f"ota {len(image)} {digest}\n".encode())
        wait_for(port, "ota: ready", timeout=10)
        started = time.monotonic()
        for offset in range(0, len(image), CHUNK):
            port.write(image[offset:offset + CHUNK])
            if args.pace:
                port.flush()
                time.sleep(args.pace)
            done = min(offset + CHUNK, len(image))
            if done % (CHUNK * 32) == 0 or done == len(image):
                log(f"sent {done * 100 // len(image)}%")
        port.flush()
        wait_for(port, "ota: ok", timeout=60)
        log(f"image accepted in {time.monotonic() - started:.1f}s; device rebooting")

    time.sleep(3)
    with open_console(args.port, timeout=30) as port:
        port.write(b"version\n")
        line = wait_for(port, "version:", timeout=10)
    log(f"running: {line[len('version:'):].strip()}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, TimeoutError, serial.SerialException) as exc:
        log(f"FAILED: {exc}")
        sys.exit(1)
