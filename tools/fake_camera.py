"""A stand-in for the DJI Osmo Action 5 Pro's side of the UDP 9004 datalink, for developing the app
without the camera.

It answers the handshake, streams a recorded live view (captures/liveview-*.bin, or any Annex-B
H.264 file with AUDs) as video datagrams, answers DUML requests, keeps a small settings state that
the app's camera controls change, and pushes status topics (00/99) in the layouts of
docs/camera-controls.md, plus a battery push. It checks the app's plumbing, not the camera's real
behaviour: the layouts are the documented ones, so a mistake in the documentation is not caught.

Point the app at it with its camera address, e.g.:
    ./obs-dji.sh fake-camera --video captures/liveview-20260924-223449.bin
    dji-vcam-cli --camera-ip 127.0.0.1 --seconds 30 --show-messages
"""
from __future__ import annotations

import argparse
import socket
import struct
import threading
import time
from pathlib import Path

import datalink
import duml

CHUNK = 1400
FRAME_INTERVAL = 1 / 30
SUBHEADER = bytes([0x68, 0x70, 0x70, 0x70, 0, 0, 0, 0, 0x09, 0x03, 0x60, 0x00])

# Capability lists (code per entry) as the camera would report them for a 16:9 video mode.
CAPABILITIES = {
    "camcap_base": [0x01, 0x05, 0x00, 0x02, 0x0A, 0x28],
    "camcap_eis": [0x00, 0x01, 0x03, 0x02, 0x04],
    "camcap_fov": [0x00, 0x01, 0x02],
    "camcap_iso": [0, 3, 4, 5, 6, 7, 8, 9, 10, 11],
    "camcap_iso_auto_max": [4, 5, 6, 7, 8, 9],
    "camcap_color_mode": [0x00, 0x3F, 0x3C, 0x3D],
    "camcap_antiflicker": [0, 1, 2, 3],
    "camcap_sharpness": [0xFE, 0xFF, 0x00, 0x01, 0x02],
    "camcap_denoise": [0xFE, 0xFF, 0x00, 0x01],
}
VIDEO_FORMATS = [(0x10, 0x03), (0x10, 0x06), (0x10, 0x07), (0x67, 0x03), (0x67, 0x06), (0x2D, 0x03),
                 (0x2D, 0x06), (0x0A, 0x03), (0x0A, 0x06), (0x0A, 0x07), (0x0A, 0x08)]


class FakeCamera:
    def __init__(self, host: str, port: int, video: Path | None) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if hasattr(socket, "SIO_UDP_CONNRESET"):  # Windows: don't fail receives once the app has gone
            self.sock.ioctl(socket.SIO_UDP_CONNRESET, False)
        self.sock.bind((host, port))
        self.app: tuple[str, int] | None = None
        self.session = 0
        self.seq = 0x1000
        self.video_seq = 0x2000
        self.parser = duml.StreamParser()
        self.lock = threading.Lock()
        self.subscribed: set[str] = set()
        self.last_heard = 0.0
        self.units = split_access_units(video.read_bytes()) if video else []
        # Settings, codes as in docs/camera-controls.md.
        self.s = {"mode": 0x01, "res": 0x10, "fps": 0x03, "codec": 0, "eis": 0x01, "fov": 0x01, "sport": 0,
                  "expo": 1, "iso": 0, "ev": 16, "shutter": 120, "iso_max": 7, "color": 0x00, "af": 0,
                  "wb": 0, "texture": 0, "nr": 0, "recording": False, "record_s": 0}

    # --- sending -----------------------------------------------------------------------------

    def send(self, pkt_type: int, payload: bytes, seq: int | None = None) -> None:
        if not self.app:
            return
        with self.lock:
            if seq is None:
                self.seq = (self.seq + 8) & 0xFFFF
                seq = self.seq
            self.sock.sendto(datalink.header(pkt_type, len(payload), self.session, seq) + payload, self.app)

    def send_frame(self, frame: duml.Frame, pkt_type: int = datalink.TYPE_STATUS) -> None:
        self.send(pkt_type, frame.encode())

    def push_topic(self, name: str) -> None:
        if name not in self.subscribed:
            return
        value = self.topic_value(name)
        body = name.encode()
        payload = (bytes([0x02, 0x06, 0, 0]) + struct.pack("<I", 1) + bytes(3) +
                   struct.pack("<HH", len(body) + 8 + len(value), len(body)) + body + bytes(6) +
                   struct.pack("<H", len(value)) + value)
        self.send_frame(duml.Frame(0x28, duml.ADDR_APP, 0, duml.FLAG_PUSH, 0x00, 0x99, payload))

    def topic_value(self, name: str) -> bytes:
        s = self.s
        if name == "cam_status":
            flags = (0x0018 if s["recording"] else 0) | (0x1000 if s["sport"] else 0)
            return struct.pack("<H", flags) + bytes([95, 1, s["mode"], 0, 0, 0, 0])
        if name == "cam_video_param_v2":
            return bytes([s["res"], s["fps"], 0, 0, 0, 0, 0, 0, s["codec"]])
        if name == "cam_expo_param":
            v = bytearray(46)
            v[5], v[6], v[7] = s["iso"], s["ev"], s["expo"]
            v[16:18] = struct.pack("<H", 400)
            v[20:22] = struct.pack("<H", 0x8000 | s["shutter"])
            return bytes(v)
        if name == "cam_image_effect":
            v = bytearray(16)
            v[2], v[3] = s["color"], s["af"]
            v[4], v[5] = (6, s["wb"] // 100) if s["wb"] else (0, 55)
            v[14], v[15] = s["nr"] & 0xFF, s["texture"] & 0xFF
            return bytes(v)
        if name == "cam_record_time":
            return struct.pack("<H", s["record_s"]) + bytes(4)
        if name == "cam_storage":
            entry = bytes([0x00, 0x01]) + struct.pack("<IIII", 30464, 15360, 999, 3600 * 3)
            return bytes([0x00, 0x00, 0x01, 0x00]) + entry
        if name == "camcap_video_format":
            entries = b"".join(bytes([r, f, 0]) for r, f in VIDEO_FORMATS)
            return bytes([1]) + struct.pack("<H", len(entries) + 1) + bytes([len(VIDEO_FORMATS)]) + entries
        if name in CAPABILITIES:
            codes = CAPABILITIES[name]
            return bytes([1]) + struct.pack("<H", len(codes) + 1) + bytes([len(codes)]) + bytes(codes)
        return b""

    # --- requests ----------------------------------------------------------------------------

    def handle(self, frame: duml.Frame) -> None:
        if not frame.is_request:
            return
        key = (frame.cmd_set, frame.cmd_id)
        p = frame.payload
        s = self.s
        ret = b"\x00"
        topics: list[str] = []
        if key == (0x00, 0x99) and len(p) >= 15 and p[1] == 0x02:  # subscribe
            name = p[15:15 + struct.unpack_from("<H", p, 13)[0]].decode(errors="replace")
            self.subscribed.add(name)
            print(f"subscribed: {name}")
            self.send_frame(frame.reply(bytes(10)), datalink.TYPE_RELIABLE)
            self.push_topic(name)
            return
        if key == (0x02, 0x8E) and len(p) >= 4:
            pid = struct.unpack_from("<H", p, 2)[0]
            names = {0x0008: "eis", 0x0009: "fov", 0x000F: "iso_max", 0x0030: "sport"}
            if pid not in names:
                self.send_frame(frame.reply(b"\xe0"), datalink.TYPE_RELIABLE)
                return
            if p[0] == 0x01 and len(p) >= 6:  # SET
                s[names[pid]] = p[5]
                print(f"set parameter 0x{pid:04x} = {p[5]}")
                topics = ["cam_status"] if pid == 0x0030 else []
            else:  # GET
                ret = bytes([0x00, 0x00, 0x01]) + struct.pack("<H", pid) + bytes([1, s[names[pid]]])
        elif key == (0x02, 0xE1):
            s["mode"] = p[0]
            topics = ["cam_status"]
        elif key == (0x02, 0x18):
            if (p[0], p[1]) not in VIDEO_FORMATS:
                ret = b"\xdf"
            elif s["recording"]:
                ret = b"\xd9"
            else:
                s["res"], s["fps"] = p[0], p[1]
                topics = ["cam_video_param_v2"]
        elif key == (0x02, 0xAB):
            s["codec"] = p[0]
            topics = ["cam_video_param_v2"]
        elif key == (0x02, 0x1E):
            s["expo"] = p[0]
            topics = ["cam_expo_param"]
        elif key == (0x02, 0x2A):
            s["iso"] = p[0]
            topics = ["cam_expo_param"]
        elif key == (0x02, 0x2E):
            s["ev"] = p[0]
            topics = ["cam_expo_param"]
        elif key == (0x02, 0x28):
            s["shutter"] = struct.unpack_from("<H", p, 1)[0] & 0x7FFF
            topics = ["cam_expo_param"]
        elif key == (0x02, 0x2C):
            s["wb"] = struct.unpack_from("<H", p, 1)[0] * 100 if p[0] else 0
            topics = ["cam_image_effect"]
        elif key in ((0x02, 0x42), (0x02, 0x46), (0x02, 0x38), (0x02, 0x44)):
            field = {0x42: "color", 0x46: "af", 0x38: "texture", 0x44: "nr"}[frame.cmd_id]
            s[field] = p[0] if field in ("color", "af") else struct.unpack("b", p[:1])[0]
            topics = ["cam_image_effect"]
        elif key == (0x02, 0x02):
            if p[0] == 1 and s["recording"]:
                ret = b"\xdf"
            else:
                s["recording"] = p[0] == 1
                s["record_s"] = 0
                topics = ["cam_status", "cam_record_time"]
        elif key == (0x02, 0x01):
            ret = b"\x00" if s["mode"] == 0x05 else b"\xd9"
        elif frame.cmd_set == 0x00:
            ret = p  # the session's own heartbeat and registration: echo like the camera
        if frame.cmd_set != 0x00:
            print(f"request {frame.describe()} -> {ret.hex()}")
        self.send_frame(frame.reply(ret), datalink.TYPE_RELIABLE)
        for topic in topics:
            self.push_topic(topic)

    # --- loops -------------------------------------------------------------------------------

    def video_loop(self) -> None:
        index = 0
        next_frame = time.monotonic()
        while True:
            next_frame += FRAME_INTERVAL
            time.sleep(max(0.0, next_frame - time.monotonic()))
            if not self.units or time.monotonic() - self.last_heard > 5:
                continue
            unit = self.units[index % len(self.units)]
            index += 1
            for start in range(0, len(unit), CHUNK):
                self.video_seq = (self.video_seq + 8) & 0xFFFF
                self.send(datalink.TYPE_VIDEO, SUBHEADER + unit[start:start + CHUNK], self.video_seq)

    def status_loop(self) -> None:
        while True:
            time.sleep(1)
            if time.monotonic() - self.last_heard > 5:
                continue
            battery = bytearray(40)
            battery[20] = 77
            self.send_frame(duml.Frame(0x05, duml.ADDR_APP, 0, duml.FLAG_PUSH, 0x0D, 0x02, bytes(battery)))
            if self.s["recording"]:
                self.s["record_s"] += 1
                self.push_topic("cam_record_time")

    def run(self) -> None:
        threading.Thread(target=self.video_loop, daemon=True).start()
        threading.Thread(target=self.status_loop, daemon=True).start()
        print(f"fake camera listening on {self.sock.getsockname()}, {len(self.units)} video frames")
        while True:
            try:
                raw, address = self.sock.recvfrom(65536)
            except ConnectionResetError:
                continue
            if len(raw) < datalink.HEADER_LEN:
                continue
            pkt_type = raw[6]
            if pkt_type == datalink.TYPE_HANDSHAKE:
                self.app = address
                self.session = struct.unpack_from("<H", raw, 2)[0]
                self.subscribed.clear()
                print(f"handshake from {address}, session {self.session:04x}")
                self.send(datalink.TYPE_HANDSHAKE, raw[datalink.HEADER_LEN:], 0)
            elif pkt_type == datalink.TYPE_COMMAND and len(raw) > datalink.HEADER_LEN + 12:
                self.last_heard = time.monotonic()
                for frame in self.parser.feed(raw[datalink.HEADER_LEN + 12:]):
                    self.handle(frame)
            elif pkt_type == datalink.TYPE_ACK:
                self.last_heard = time.monotonic()


def split_access_units(stream: bytes) -> list[bytes]:
    """Splits an Annex-B stream at its access unit delimiters (NAL type 9)."""
    marker = b"\x00\x00\x00\x01\x09"
    starts = []
    at = stream.find(marker)
    while at >= 0:
        starts.append(at)
        at = stream.find(marker, at + 1)
    return [stream[a:b] for a, b in zip(starts, starts[1:] + [len(stream)])]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9004)
    parser.add_argument("--video", type=Path, help="Annex-B H.264 file to stream (e.g. a captures/liveview-*.bin)")
    args = parser.parse_args()
    FakeCamera(args.host, args.port, args.video).run()


if __name__ == "__main__":
    main()
