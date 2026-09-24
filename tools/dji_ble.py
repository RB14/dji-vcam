"""Pair with a DJI Osmo Action camera over BLE and read its Wi-Fi AP credentials.

Sequence (see docs/protocol.md): arm the notify characteristic, open the session, SetPairingPIN,
wait for the on-camera approval on first use, wake the AP, then read SSID and passphrase.
Every request the camera sends us is acknowledged, otherwise it drops the link.

Runs on Windows Python (BLE lives on the Windows host). Examples:
    dji_ble.py scan
    dji_ble.py creds                       # pair + print/save SSID and password
    dji_ble.py creds --bridge COM8         # ...and hand them to the ESP32 USB Wi-Fi bridge
"""

from __future__ import annotations

import argparse
import asyncio
import json
import secrets
import sys
import time
from pathlib import Path

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

import duml

DJI_COMPANY_ID = 0x08AA
MODEL_ACTION_5_PRO = 0x15
CHAR_NOTIFY = "0000fff4-0000-1000-8000-00805f9b34fb"
CHAR_WRITE = "0000fff5-0000-1000-8000-00805f9b34fb"
PAIRING_TOKEN = "obsd"  # shown on the camera's approval prompt

STATE_DIR = Path(__file__).resolve().parent.parent / ".state"
IDENTIFIER_FILE = STATE_DIR / "ble_identifier"
CREDENTIALS_FILE = STATE_DIR / "camera_wifi.json"

# 62-byte "APP" device-info blob the camera expects in reply to its 0x00/0x81 request.
APP_DEVICE_INFO = b"\x00APP" + bytes(37) + b"\x02" + bytes(8) + b"\x02\x08" + bytes(10)


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def load_identifier() -> str:
    """Per-install pairing identifier; the camera remembers approvals keyed on it."""
    if IDENTIFIER_FILE.exists():
        return IDENTIFIER_FILE.read_text().strip()
    STATE_DIR.mkdir(exist_ok=True)
    identifier = secrets.token_hex(16)
    IDENTIFIER_FILE.write_text(identifier)
    return identifier


def dji_model(manufacturer_data: dict[int, bytes]) -> int | None:
    payload = manufacturer_data.get(DJI_COMPANY_ID)
    return payload[0] if payload else None


async def find_camera(seconds: float) -> BLEDevice | None:
    """Returns the first DJI camera seen. Cameras advertise only while awake, so stop early."""
    def is_camera(device: BLEDevice, adv) -> bool:
        name = device.name or adv.local_name or ""
        return dji_model(adv.manufacturer_data) is not None or name.startswith("Osmo")

    log(f"scanning up to {seconds:.0f}s for a DJI camera (wake it up if it is asleep)")
    device = await BleakScanner.find_device_by_filter(is_camera, timeout=seconds)
    if device is not None:
        log(f"found {device.name!r} {device.address}")
    return device


class CameraLink:
    """DUML request/response over the camera's fff4 (notify) / fff5 (write) characteristics."""

    def __init__(self, client: BleakClient, verbose: bool) -> None:
        self._client = client
        self._verbose = verbose
        self._parser = duml.StreamParser()
        self._waiters: list[tuple[int, int, asyncio.Future[duml.Frame]]] = []
        self._seq = 0x8000 + secrets.randbelow(0x1000)
        self.approved = asyncio.Event()

    def _next_seq(self) -> int:
        self._seq = (self._seq + 1) & 0xFFFF
        return self._seq

    def on_notify(self, _sender: object, data: bytearray) -> None:
        for frame in self._parser.feed(bytes(data)):
            if self._verbose:
                log(f"<- {frame.describe()}")
            if frame.is_request:
                asyncio.ensure_future(self._answer(frame))
                continue
            for waiter in list(self._waiters):
                cmd_set, cmd_id, future = waiter
                if (frame.cmd_set, frame.cmd_id) == (cmd_set, cmd_id) and not future.done():
                    future.set_result(frame)
                    self._waiters.remove(waiter)

    async def _answer(self, request: duml.Frame) -> None:
        if (request.cmd_set, request.cmd_id) == (0x07, 0x46):
            log("camera approved the pairing")
            self.approved.set()
        payload = APP_DEVICE_INFO if (request.cmd_set, request.cmd_id) == (0x00, 0x81) else request.payload
        await self._write(request.reply(payload))

    async def _write(self, frame: duml.Frame) -> None:
        if self._verbose:
            log(f"-> {frame.describe()}")
        await self._client.write_gatt_char(CHAR_WRITE, frame.encode(), response=False)

    async def send(self, receiver: int, cmd_set: int, cmd_id: int, payload: bytes = b"",
                   flags: int = duml.FLAG_REQUEST, seq: int | None = None) -> None:
        seq = self._next_seq() if seq is None else seq
        await self._write(duml.Frame(duml.ADDR_APP, receiver, seq, flags, cmd_set, cmd_id, payload))

    async def request(self, receiver: int, cmd_set: int, cmd_id: int, payload: bytes = b"",
                      timeout: float = 2.0, seq: int | None = None) -> duml.Frame | None:
        future: asyncio.Future[duml.Frame] = asyncio.get_running_loop().create_future()
        self._waiters.append((cmd_set, cmd_id, future))
        await self.send(receiver, cmd_set, cmd_id, payload, seq=seq)
        try:
            return await asyncio.wait_for(future, timeout)
        except asyncio.TimeoutError:
            if (cmd_set, cmd_id, future) in self._waiters:
                self._waiters.remove((cmd_set, cmd_id, future))
            return None


def mask(secret: str) -> str:
    """Keeps secrets out of logs and terminals."""
    return f"<{len(secret)} chars>"


def parse_string_reply(frame: duml.Frame | None) -> str | None:
    """Replies to GetWifiSsid / GetWifiPassword are [status u8][len u8][ascii]."""
    if frame is None or len(frame.payload) < 2 or frame.payload[0] != 0:
        return None
    length = frame.payload[1]
    return frame.payload[2:2 + length].decode("utf-8", errors="replace")


async def pair(link: CameraLink, approval_timeout: float) -> bool:
    identifier = load_identifier()
    pairing = duml.pack_string(identifier) + duml.pack_string(PAIRING_TOKEN)
    seq = 0x8092
    for attempt in range(3):
        reply = await link.request(duml.ADDR_WIFI, 0x07, 0x45, pairing, timeout=2.5, seq=seq)
        if reply is None:
            log(f"no pairing reply (attempt {attempt + 1})")
            continue
        status = reply.payload[1] if len(reply.payload) > 1 else None
        if status == 0x01:
            log("already paired")
            return True
        if status == 0x02:
            log(f">>> APPROVE THE PAIRING PROMPT ON THE CAMERA SCREEN (token '{PAIRING_TOKEN.upper()}') <<<")
            try:
                await asyncio.wait_for(link.approved.wait(), approval_timeout)
                return True
            except asyncio.TimeoutError:
                log("approval timed out")
                return False
        log(f"unexpected pairing reply {reply.payload.hex()}")
    return False


async def read_credentials(args: argparse.Namespace) -> dict[str, str] | None:
    device = await find_camera(args.scan_seconds)
    if device is None:
        log("no DJI camera advertising; is it on, awake, with wireless enabled?")
        return None
    async with BleakClient(device, timeout=20.0) as client:
        log(f"connected, MTU {client.mtu_size}")
        link = CameraLink(client, args.verbose)
        await client.start_notify(CHAR_NOTIFY, link.on_notify)
        try:
            await client.start_notify(CHAR_WRITE, link.on_notify)
        except Exception as exc:  # the write characteristic may not support notify
            log(f"notify on fff5 unavailable: {exc}")
        await client.write_gatt_char(CHAR_NOTIFY, b"\x01\x00", response=True)  # arms pairing
        await asyncio.sleep(0.2)

        await link.send(duml.ADDR_SESSION, 0x00, 0x2B, b"\x04\x00")  # session open (Mimo does this first)
        await asyncio.sleep(0.12)
        if not await pair(link, args.approval_timeout):
            return None

        await asyncio.sleep(0.1)
        wake = await link.request(duml.ADDR_SYSTEM, 0x53, 0x10, bytes(4), timeout=1.5)
        log(f"wake reply: {wake.payload.hex() if wake else 'none'}")
        await asyncio.sleep(0.8)
        ssid = parse_string_reply(await link.request(duml.ADDR_WIFI, 0x07, 0x07))
        await asyncio.sleep(0.5)
        password = parse_string_reply(await link.request(duml.ADDR_WIFI, 0x07, 0x0E))
        if not ssid or password is None:
            log(f"could not read credentials (ssid={ssid!r}); camera may not be activated")
            return None
        creds = {"ssid": ssid, "password": password, "ble_address": device.address}
        STATE_DIR.mkdir(exist_ok=True)
        CREDENTIALS_FILE.write_text(json.dumps(creds, indent=2))
        log(f"camera AP: ssid={ssid!r} password={mask(password)} (saved to {CREDENTIALS_FILE.name})")

        if args.bridge:
            push_to_bridge(args.bridge, ssid, password)
        if args.hold:
            log(f"holding BLE link for {args.hold:.0f}s (keepalive 00/2b every 1s)")
            deadline = time.monotonic() + args.hold
            while time.monotonic() < deadline:
                await link.send(duml.ADDR_SESSION, 0x00, 0x2B, b"\x01\x01")
                await asyncio.sleep(1.0)
        return creds


def push_to_bridge(port_name: str, ssid: str, password: str) -> None:
    import serial

    with serial.Serial(port_name, 115200, timeout=0.5) as port:
        port.dtr = True
        port.write(f'wifi "{ssid}" "{password}"\n'.encode())
        time.sleep(0.5)
        reply = port.read(port.in_waiting or 1).decode(errors="replace").strip()
        log(f"bridge: {reply.replace(password, mask(password))}")


async def scan(args: argparse.Namespace) -> None:
    found = await BleakScanner.discover(timeout=args.scan_seconds, return_adv=True)
    for device, adv in sorted(found.values(), key=lambda item: -(item[1].rssi or -999)):
        model = dji_model(adv.manufacturer_data)
        if model is None and not args.all:
            continue
        tag = f"DJI model=0x{model:02x}" if model is not None else "   "
        print(f"{tag} {device.address} rssi={adv.rssi} name={device.name or adv.local_name!r}")
        for company_id, payload in adv.manufacturer_data.items():
            print(f"      mfr 0x{company_id:04x}: {payload.hex()}")
        for uuid, data in adv.service_data.items():
            print(f"      svc-data {uuid}: {data.hex()}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("action", choices=["scan", "creds"])
    parser.add_argument("--scan-seconds", type=float, default=10.0)
    parser.add_argument("--approval-timeout", type=float, default=60.0)
    parser.add_argument("--bridge", help="COM port of the ESP32 USB Wi-Fi bridge console")
    parser.add_argument("--hold", type=float, default=0.0, help="keep the BLE link open this many seconds")
    parser.add_argument("-v", "--verbose", action="store_true", help="log every DUML frame")
    parser.add_argument("--all", action="store_true", help="scan: list non-DJI devices too")
    args = parser.parse_args()
    if args.action == "scan":
        asyncio.run(scan(args))
    else:
        sys.exit(0 if asyncio.run(read_credentials(args)) else 1)


if __name__ == "__main__":
    main()
