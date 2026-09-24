"""DJI DUML framing (SOF 0x55), as spoken over BLE, TCP 7001 and the UDP 9004 datalink.

Frame layout:
    0     SOF 0x55
    1..2  u16-LE: bits[9:0] total length (13 + payload), bits[15:10] version (1)
    3     CRC8 over bytes [0:3]
    4     sender   (id << 5) | type
    5     receiver (id << 5) | type
    6..7  sequence / message id (camera echoes it verbatim)
    8     flags (cmd_type << 5) | encrypt: 0x40 request, 0xC0 response, 0x00 push
    9     command set
    10    command id
    11..  payload
    -2..  CRC16-LE over everything before it
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

SOF = 0x55
HEADER_LEN = 11
OVERHEAD = 13
VERSION_BITS = 0x0400

FLAG_PUSH = 0x00
FLAG_REQUEST = 0x40
FLAG_WRITE = 0x80
FLAG_RESPONSE = 0xC0

# Endpoint addresses, (id << 5) | type.
ADDR_CAMERA = 0x01
ADDR_APP = 0x02
ADDR_WIFI = 0x07
ADDR_DM368_1 = 0x28
ADDR_DM368_2 = 0x48
ADDR_SESSION = 0xF0
ADDR_SYSTEM = 0x1C


def _crc8_table() -> list[int]:
    table = []
    for byte in range(256):
        crc = byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8C if crc & 1 else crc >> 1
        table.append(crc)
    return table


def _crc16_table() -> list[int]:
    table = []
    for byte in range(256):
        crc = byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
        table.append(crc)
    return table


_CRC8 = _crc8_table()
_CRC16 = _crc16_table()


def crc8(data: bytes) -> int:
    crc = 0x77
    for byte in data:
        crc = _CRC8[(crc ^ byte) & 0xFF]
    return crc


def crc16(data: bytes) -> int:
    crc = 0x3692
    for byte in data:
        crc = (crc >> 8) ^ _CRC16[(crc ^ byte) & 0xFF]
    return crc


def pack_string(text: str) -> bytes:
    raw = text.encode("utf-8")
    return bytes([len(raw)]) + raw


@dataclass(frozen=True)
class Frame:
    sender: int
    receiver: int
    seq: int
    flags: int
    cmd_set: int
    cmd_id: int
    payload: bytes = b""

    @property
    def is_request(self) -> bool:
        return self.flags & 0xE0 == FLAG_REQUEST

    @property
    def is_response(self) -> bool:
        return self.flags & 0x80 == 0x80 and self.flags & 0x40 == 0x40

    def encode(self) -> bytes:
        total = OVERHEAD + len(self.payload)
        if total > 0x3FF:
            raise ValueError(f"DUML frame too long: {total}")
        head = bytearray(struct.pack("<BH", SOF, VERSION_BITS | total))
        head.append(crc8(head))
        head += bytes([self.sender, self.receiver])
        head += struct.pack("<H", self.seq & 0xFFFF)
        head += bytes([self.flags, self.cmd_set, self.cmd_id])
        head += self.payload
        return bytes(head) + struct.pack("<H", crc16(head))

    def reply(self, payload: bytes) -> Frame:
        """Builds the response to this request: addresses swapped, same sequence."""
        return Frame(self.receiver, self.sender, self.seq, FLAG_RESPONSE, self.cmd_set, self.cmd_id, payload)

    def describe(self) -> str:
        kind = {FLAG_REQUEST: "req", FLAG_RESPONSE: "rsp", FLAG_PUSH: "push"}.get(self.flags, f"f{self.flags:02x}")
        return (f"{self.sender:02x}->{self.receiver:02x} {kind} {self.cmd_set:02x}/{self.cmd_id:02x} "
                f"seq={self.seq:04x} payload={self.payload.hex()}")


def decode(raw: bytes) -> Frame:
    """Decodes exactly one complete, CRC-valid frame."""
    if len(raw) < OVERHEAD or raw[0] != SOF:
        raise ValueError("not a DUML frame")
    total = struct.unpack_from("<H", raw, 1)[0] & 0x3FF
    if total != len(raw):
        raise ValueError(f"length mismatch: header {total}, got {len(raw)}")
    if crc8(raw[:3]) != raw[3]:
        raise ValueError("bad header CRC8")
    if crc16(raw[:-2]) != struct.unpack_from("<H", raw, total - 2)[0]:
        raise ValueError("bad CRC16")
    seq = struct.unpack_from("<H", raw, 6)[0]
    return Frame(raw[4], raw[5], seq, raw[8], raw[9], raw[10], bytes(raw[HEADER_LEN:-2]))


class StreamParser:
    """Reassembles DUML frames from a byte stream (BLE notifications, TCP), skipping junk.

    Frames starting with 0xAA (DJI R-SDK protocol, also seen on the camera's notify
    characteristic) are skipped whole.
    """

    def __init__(self) -> None:
        self._buf = bytearray()

    def feed(self, data: bytes) -> list[Frame]:
        self._buf += data
        frames: list[Frame] = []
        while self._buf:
            if self._buf[0] not in (SOF, 0xAA):
                del self._buf[0]
                continue
            if len(self._buf) < 4:
                break
            total = struct.unpack_from("<H", self._buf, 1)[0] & 0x3FF
            if self._buf[0] == SOF and (total < OVERHEAD or crc8(self._buf[:3]) != self._buf[3]):
                del self._buf[0]
                continue
            if total < 4:
                del self._buf[0]
                continue
            if len(self._buf) < total:
                break
            raw = bytes(self._buf[:total])
            del self._buf[:total]
            if raw[0] == SOF:
                try:
                    frames.append(decode(raw))
                except ValueError:
                    pass
        return frames
