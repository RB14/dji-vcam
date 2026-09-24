"""DJI camera Wi-Fi datalink: the UDP 9004 transport DJI Mimo uses on the camera's own AP.

Every datagram starts with an 8-byte header (all little-endian):
    [0:2] 0x8000 | total length   [2:4] session id   [4:6] seq   [6] packet type   [7] XOR of [0:7]

Packet types: 0x00 handshake, 0x01 camera status/DUML pushes, 0x02 video, 0x03 reliable data,
0x04 window ACK (app -> camera), 0x05 command (12-byte routing header + DUML).
Layouts follow osmosis (verified on the Action 5 Pro); see docs/protocol-notes.md section (b).
"""

from __future__ import annotations

import random
import socket
import struct
import time
from collections import Counter
from dataclasses import dataclass, field

import duml

CAMERA_IP = "192.168.2.1"
DATALINK_PORT = 9004
POKE_PORT = 7001

TYPE_HANDSHAKE = 0x00
TYPE_STATUS = 0x01
TYPE_VIDEO = 0x02
TYPE_RELIABLE = 0x03
TYPE_ACK = 0x04
TYPE_COMMAND = 0x05

HEADER_LEN = 8
ROUTING_LEN = 12
STATUS_FRAME_LEN = 34
VIDEO_STALL_S = 0.3
# Window 100, MTU 1472 (c0 05) and the rest as DJI Mimo sends it; prefixed by our base sequence.
HANDSHAKE_TAIL = bytes.fromhex("64006400c005140000640000019001c005140000640014006400c00514000064000101040102")


def header(pkt_type: int, payload_len: int, session_id: int, seq: int) -> bytes:
    head = struct.pack("<HHHB", 0x8000 | ((HEADER_LEN + payload_len) & 0x3FFF), session_id, seq & 0xFFFF, pkt_type)
    xor = 0
    for byte in head:
        xor ^= byte
    return head + bytes([xor])


def seq_ahead(new: int, old: int) -> bool:
    """True if 16-bit sequence `new` is ahead of `old` (with wrap-around)."""
    return 0 < ((new - old) & 0xFFFF) < 0x8000


def seq_near(a: int, b: int, window: int = 1024) -> bool:
    """True if 16-bit sequences `a` and `b` are within `window` of each other (with wrap-around)."""
    diff = (a - b) & 0xFFFF
    return diff < window or diff > 0x10000 - window


def routing_header(seq: int, cmd_counter: int) -> bytes:
    """Command routing header: [ack = our previous seq][our seq][0 x4][counter][01][00 00]."""
    return struct.pack("<HH4xBB2x", (seq - 8) & 0xFFFF, seq & 0xFFFF, cmd_counter & 0xFF, 0x01)


@dataclass
class Datagram:
    pkt_type: int
    seq: int
    raw: bytes

    @property
    def payload(self) -> bytes:
        return self.raw[HEADER_LEN:]


@dataclass
class LinkStats:
    by_type: Counter = field(default_factory=Counter)
    bytes_by_type: Counter = field(default_factory=Counter)


class Datalink:
    def __init__(self, camera_ip: str = CAMERA_IP, port: int = DATALINK_PORT, local_ip: str = "") -> None:
        self.camera = (camera_ip, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        self.sock.bind((local_ip, 0))
        self.session_id = random.randint(0x1000, 0xFFFE)
        self.base = random.randint(0x1000, 0xF000) & 0xFFF8
        self.seq = 0
        self.camera_channel = self.base
        self.cmd_counter = 0
        self.duml_seq = 0xA000
        self.video_cursor = 0
        self.download_cursor = 0
        self.last_video_seq: int | None = None
        self.last_video_time = 0.0
        self.stats = LinkStats()
        self._parser = duml.StreamParser()

    # ---- transmit ---------------------------------------------------------------------------

    def _send(self, pkt_type: int, payload: bytes, seq: int | None = None) -> None:
        pkt_seq = self.seq if seq is None else seq
        self.sock.sendto(header(pkt_type, len(payload), self.session_id, pkt_seq) + payload, self.camera)
        if seq is None:
            self.seq = (self.seq + 8) & 0xFFFF

    def send_frame(self, frame: duml.Frame) -> None:
        self.cmd_counter += 1
        self._send(TYPE_COMMAND, routing_header(self.seq, self.cmd_counter) + frame.encode())

    def send_duml(self, receiver: int, cmd_set: int, cmd_id: int, payload: bytes = b"",
                  flags: int = duml.FLAG_REQUEST) -> None:
        frame = duml.Frame(duml.ADDR_APP, receiver, self.duml_seq, flags, cmd_set, cmd_id, payload)
        self.duml_seq = (self.duml_seq + 1) & 0xFFFF
        self.send_frame(frame)

    def send_ack(self) -> None:
        """Window ACK: [start][end][u32 0] for video, download and control, plus u16 0. Seq 0."""
        # While video flows, ack what we received; when it stalls, fall back to the camera's own
        # video cursor from its status frames so a stuck window can always recover.
        video_fresh = time.monotonic() - self.last_video_time < VIDEO_STALL_S
        video = self.last_video_seq if self.last_video_seq is not None and video_fresh else self.video_cursor
        payload = b"".join(struct.pack("<HHI", v, v, 0) for v in (video, self.download_cursor, self.base))
        self._send(TYPE_ACK, payload + b"\x00\x00", seq=0)

    # ---- receive ----------------------------------------------------------------------------

    def recv(self, timeout: float) -> list[Datagram]:
        """Receives everything that arrives within `timeout` seconds."""
        out: list[Datagram] = []
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return out
            self.sock.settimeout(remaining)
            try:
                raw, _ = self.sock.recvfrom(65536)
            except (socket.timeout, ConnectionResetError):
                return out
            if len(raw) < HEADER_LEN:
                continue
            pkt_type, seq = raw[6], struct.unpack_from("<H", raw, 4)[0]
            self.stats.by_type[pkt_type] += 1
            self.stats.bytes_by_type[pkt_type] += len(raw)
            if len(raw) >= 10:
                channel = struct.unpack_from("<H", raw, 8)[0]
                if channel:
                    self.camera_channel = channel
            if pkt_type == TYPE_STATUS and len(raw) == STATUS_FRAME_LEN:
                self.video_cursor = struct.unpack_from("<H", raw, 10)[0]
                self.download_cursor = struct.unpack_from("<H", raw, 18)[0]
            if pkt_type == TYPE_VIDEO:
                # Move the video ACK forward only: a late or retransmitted packet must not rewind it,
                # or the camera re-sends the window. A far jump is the camera restarting its video
                # stream (e.g. after a recording-format change) and is accepted in either direction.
                last = self.last_video_seq
                if last is None or seq_ahead(seq, last) or not seq_near(seq, last):
                    self.last_video_seq = seq
                self.last_video_time = time.monotonic()
            out.append(Datagram(pkt_type, seq, raw))

    def duml_frames(self, datagram: Datagram) -> list[duml.Frame]:
        if datagram.pkt_type in (TYPE_STATUS, TYPE_RELIABLE, TYPE_COMMAND):
            return self._parser.feed(datagram.payload)
        return []

    # ---- session setup ----------------------------------------------------------------------

    def poke(self, identifier: str, token: str) -> bool:
        """TCP 7001 'poke' with a SetPairingPIN frame; arms the datalink on 9004 bodies."""
        frame = duml.Frame(duml.ADDR_APP, duml.ADDR_WIFI, 0x8092, duml.FLAG_REQUEST, 0x07, 0x45,
                           duml.pack_string(identifier) + duml.pack_string(token))
        try:
            with socket.create_connection((self.camera[0], POKE_PORT), timeout=1.2) as tcp:
                tcp.sendall(frame.encode())
                time.sleep(0.4)
            return True
        except OSError:
            return False

    def handshake(self, attempts: int = 20) -> Datagram | None:
        payload = struct.pack("<H", self.base) + HANDSHAKE_TAIL
        for _ in range(attempts):
            self._send(TYPE_HANDSHAKE, payload, seq=0)
            for datagram in self.recv(0.35):
                if datagram.pkt_type == TYPE_HANDSHAKE:
                    return datagram
        return None

    def settle(self, rounds: int = 5) -> None:
        """Drains the camera's first packets (acking each batch), then syncs our seq to its channel."""
        for _ in range(rounds):
            self.recv(0.4)
            self.send_ack()
        self.seq = (self.camera_channel + 8) & 0xFFFF

    def close(self) -> None:
        self.sock.close()
