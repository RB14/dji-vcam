"""Send commands to the ESP32-S3 USB Wi-Fi bridge console and print its output.

Examples:
    bridge_console.py --port COM8 status
    bridge_console.py --port COM8 scan --wait 6
    bridge_console.py --port COM8 wifi "OsmoAction5Pro-1234" "password"
    bridge_console.py --port COM8 --listen 30          # just tail the log
"""

from __future__ import annotations

import argparse
import shlex
import sys
import time

import serial


def pump(port: serial.Serial, seconds: float) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            chunk = port.read(port.in_waiting or 1)
        except serial.SerialException:
            print("\n[port closed by device]")
            return
        if chunk:
            sys.stdout.write(chunk.decode("utf-8", errors="replace").replace("\r\n", "\n"))
            sys.stdout.flush()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--wait", type=float, default=1.5, help="seconds to collect output per command")
    parser.add_argument("--listen", type=float, default=0.0, help="seconds to keep printing output at the end")
    parser.add_argument("command", nargs="*", help="command words; quote arguments that contain spaces")
    args = parser.parse_args()

    with serial.Serial(args.port, 115200, timeout=0.1) as port:
        port.dtr = True
        time.sleep(0.2)
        port.reset_input_buffer()
        if args.command:
            line = " ".join(shlex.quote(word) if " " in word else word for word in args.command)
            port.write(line.encode() + b"\n")
            pump(port, args.wait)
        if args.listen:
            pump(port, args.listen)


if __name__ == "__main__":
    main()
