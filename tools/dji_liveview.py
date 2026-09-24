"""Experiment: open the camera datalink over its AP and try to start the live view.

Run it while the camera AP is up and reachable (ESP32 bridge joined, laptop has 192.168.2.x),
e.g. right after `dji_ble.py creds --hold 90` woke the AP.

Sequence (docs/protocol-notes.md section 3.9): TCP 7001 poke, UDP 9004 handshake, settle,
register (0x00/0x81 + 0x00/0x88), then the Pocket 3 live-view trigger (0x00/0x81 + 0x00/0x82 and a
0x00/0x4F heartbeat every 200 ms). Video datagrams (type 0x02) are stripped of their 12-byte
sub-header and written to captures/liveview-<time>.bin for analysis.
"""

from __future__ import annotations

import argparse
import socket
import struct
import subprocess
import time
from collections import Counter, deque
from pathlib import Path

import datalink
import duml
from dji_ble import APP_DEVICE_INFO, PAIRING_TOKEN, load_identifier
from h264_sps import SpsInfo, parse_sps

CAPTURE_DIR = Path(__file__).resolve().parent.parent / "captures"
REGISTER_88 = bytes.fromhex("170046237c415050000000000002")
VIDEO_SUBHEADER_LEN = 12
ACK_INTERVAL = 0.03
HEARTBEAT_INTERVAL = 0.2
REGISTER_INTERVAL = 1.0
TRIGGER_INTERVAL = 2.0
# Low-latency ffplay: no input buffering, no probing, drop late frames.
FFPLAY_CMD = ["ffplay", "-hide_banner", "-loglevel", "error", "-fflags", "nobuffer", "-flags", "low_delay",
              "-framedrop", "-probesize", "32", "-analyzeduration", "0",
              # Raw H.264 has no timestamps; the demuxer's default 25 fps would pace playback below the
              # camera's ~30 fps and delay would grow. 60 fps pacing just shows frames as they arrive.
              "-framerate", "60", "-window_title", "DJI live view", "-f", "h264", "-i", "-"]


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def local_ip_towards(ip: str) -> str:
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((ip, datalink.DATALINK_PORT))
        return probe.getsockname()[0]
    finally:
        probe.close()


def wait_for_camera_route(timeout: float) -> str | None:
    """Waits until this host has an address on the camera subnet (i.e. the bridge joined the AP)."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ip = local_ip_towards(datalink.CAMERA_IP)
        if ip.startswith("192.168.2."):
            return ip
        time.sleep(0.5)
    return None


def nal_summary(stream: bytes) -> str:
    """Classifies Annex-B NAL units as H.264 or HEVC by their header bytes."""
    h264, hevc = Counter(), Counter()
    i = stream.find(b"\x00\x00\x01")
    while 0 <= i < len(stream) - 3:
        nal = stream[i + 3]
        h264[nal & 0x1F] += 1
        hevc[(nal >> 1) & 0x3F] += 1
        i = stream.find(b"\x00\x00\x01", i + 3)
    if not h264:
        return "no Annex-B start codes found"
    return (f"as H.264 types {dict(h264.most_common(6))} | as HEVC types {dict(hevc.most_common(6))} "
            "(H.264: 7=SPS 8=PPS 5=IDR 1=slice; HEVC: 32=VPS 33=SPS 34=PPS 19/20=IDR 1=slice)")


class StreamWatcher:
    """Watches the H.264 byte stream for format changes (SPS) and counts frames (AUDs)."""

    START = b"\x00\x00\x01"
    AUD = b"\x00\x00\x01\x09"

    def __init__(self) -> None:
        self._tail = b""
        self._sps = b""
        self.info: SpsInfo | None = None
        self.frames = 0

    def feed(self, data: bytes) -> SpsInfo | None:
        """Returns the new stream format when it changes."""
        buf = self._tail + data
        self.frames += buf.count(self.AUD) - self._tail.count(self.AUD)
        changed = None
        i = buf.find(self.START)
        while 0 <= i < len(buf) - 3:
            if buf[i + 3] & 0x1F == 7:
                end = buf.find(self.START, i + 3)
                if end < 0:
                    break  # SPS not complete yet; the tail keeps it for the next feed
                nal = buf[i + 3:end].rstrip(b"\x00")
                if nal != self._sps:
                    self._sps = nal
                    info = parse_sps(nal)
                    if info != self.info:
                        changed = self.info = info
            i = buf.find(self.START, i + 3)
        self._tail = buf[-64:]
        return changed


def decode_ability_payload(resolution: str) -> bytes:
    """SendAppDecodeAbility (0x09/0xFD) TLV list: [count] then [type u8][value u32-LE] per item.
    Type 1 = resolution, encoded as width (u16-LE) followed by height (u16-LE)."""
    width, height = (int(v) for v in resolution.lower().split("x"))
    return bytes([1, 1]) + struct.pack("<HH", width, height)


def send_decode_ability(link: datalink.Datalink, payload: bytes | None) -> None:
    if payload is not None:
        link.send_duml(duml.ADDR_DM368_2, 0x09, 0xFD, payload)


def start_live_view(link: datalink.Datalink) -> None:
    link.send_duml(duml.ADDR_DM368_2, 0x00, 0x81, APP_DEVICE_INFO, flags=duml.FLAG_WRITE)
    link.send_duml(duml.ADDR_DM368_2, 0x00, 0x82, b"\x00", flags=duml.FLAG_WRITE)


def run(args: argparse.Namespace) -> int:
    local_ip = wait_for_camera_route(args.wait)
    if local_ip is None:
        log("no address on 192.168.2.x: is the bridge joined to the camera AP?")
        return 1
    log(f"camera subnet reachable from {local_ip}")

    link = datalink.Datalink(local_ip=local_ip, port=args.port)
    log(f"tcp 7001 poke: {'ok' if link.poke(load_identifier(), PAIRING_TOKEN) else 'refused'}")
    reply = link.handshake()
    if reply is None:
        log(f"handshake failed on udp/{args.port}")
        return 1
    log(f"handshake ok: session=0x{link.session_id:04x} base=0x{link.base:04x} reply={reply.raw.hex()}")
    link.settle()
    log(f"settled: camera channel=0x{link.camera_channel:04x}, our seq=0x{link.seq:04x}, "
        f"packets so far {dict(link.stats.by_type)}")

    link.send_duml(duml.ADDR_DM368_2, 0x00, 0x81, APP_DEVICE_INFO, flags=duml.FLAG_WRITE)
    link.recv(0.3)
    link.send_duml(duml.ADDR_DM368_1, 0x00, 0x88, REGISTER_88)
    link.recv(0.3)
    link.send_ack()
    ability = decode_ability_payload(args.decode_ability) if args.decode_ability else None
    if ability is not None:
        log(f"sending SendAppDecodeAbility 09/fd payload {ability.hex()}")
    send_decode_ability(link, ability)
    start_live_view(link)
    log("registered; live-view trigger sent, streaming heartbeat")

    CAPTURE_DIR.mkdir(exist_ok=True)
    out_path = CAPTURE_DIR / f"liveview-{time.strftime('%Y%m%d-%H%M%S')}.bin"
    subheaders: list[str] = []
    recent_video_seqs: deque[int] = deque(maxlen=512)
    watcher = StreamWatcher()
    last_frames = 0
    duplicates = 0
    seen_frames: Counter = Counter()
    video_bytes = 0
    hb_counter = hb_ticks = 0
    now = time.monotonic()
    end = now + args.seconds
    next_ack = next_hb = now
    next_register = now + REGISTER_INTERVAL
    next_trigger = now + TRIGGER_INTERVAL
    next_report = now + 1.0
    last_report_bytes = 0

    player = subprocess.Popen(FFPLAY_CMD, stdin=subprocess.PIPE) if args.play else None
    with out_path.open("wb") as video_out:
        while time.monotonic() < end:
            for datagram in link.recv(0.01):
                if datagram.pkt_type == datalink.TYPE_VIDEO:
                    if datagram.seq in recent_video_seqs:
                        duplicates += 1
                        continue
                    recent_video_seqs.append(datagram.seq)
                    body = datagram.payload
                    if len(subheaders) < 12:
                        subheaders.append(body[:VIDEO_SUBHEADER_LEN].hex())
                    video_out.write(body[VIDEO_SUBHEADER_LEN:])
                    if (new_format := watcher.feed(body[VIDEO_SUBHEADER_LEN:])) is not None:
                        log(f"PREVIEW FORMAT: {new_format}")
                    if player is not None:
                        try:
                            player.stdin.write(body[VIDEO_SUBHEADER_LEN:])
                            player.stdin.flush()
                        except OSError:
                            player = None  # window closed
                    video_bytes += max(0, len(body) - VIDEO_SUBHEADER_LEN)
                for frame in link.duml_frames(datagram):
                    seen_frames[(frame.cmd_set, frame.cmd_id)] += 1
                    if frame.cmd_set == 0x09:
                        log(f"<- {frame.describe()}")
                    if frame.is_request:
                        if args.verbose:
                            log(f"<- {frame.describe()}")
                        is_info = (frame.cmd_set, frame.cmd_id) == (0x00, 0x81)
                        link.send_frame(frame.reply(APP_DEVICE_INFO if is_info else frame.payload))

            now = time.monotonic()
            if now >= next_ack:
                link.send_ack()
                next_ack = now + ACK_INTERVAL
            if now >= next_hb:
                payload = struct.pack("<BBBBBI", 0x01, 0x00, hb_counter & 0xFF, 0x00, 0x00, 0xFFFFFFFF)
                link.send_duml(duml.ADDR_DM368_2, 0x00, 0x4F, payload)
                hb_ticks += 1
                if hb_ticks % 2 == 0:
                    hb_counter += 1
                next_hb = now + HEARTBEAT_INTERVAL
            if now >= next_register:
                link.send_duml(duml.ADDR_DM368_1, 0x00, 0x88, REGISTER_88)
                next_register = now + REGISTER_INTERVAL
            if now >= next_trigger:
                send_decode_ability(link, ability)
                start_live_view(link)
                next_trigger = now + TRIGGER_INTERVAL
            if now >= next_report:
                kbps = (video_bytes - last_report_bytes) * 8 / 1000
                last_report_bytes = video_bytes
                fps = watcher.frames - last_frames
                last_frames = watcher.frames
                log(f"packets {dict(sorted(link.stats.by_type.items()))} video {kbps:.0f} kbit/s {fps} fps")
                next_report = now + 1.0

    link.close()
    if player is not None:
        player.stdin.close()
        player.terminate()
    log(f"done: {video_bytes} video bytes -> {out_path.name}, {duplicates} duplicate packets dropped")
    log(f"duml frames seen (set/id: count): "
        f"{ {f'{s:02x}/{i:02x}': n for (s, i), n in seen_frames.most_common(15)} }")
    if subheaders:
        log("first video sub-headers: " + " ".join(subheaders[:6]))
        log(nal_summary(out_path.read_bytes()[:2_000_000]))
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seconds", type=float, default=20.0, help="how long to stream")
    parser.add_argument("--wait", type=float, default=60.0, help="max seconds to wait for the camera subnet")
    parser.add_argument("--port", type=int, default=datalink.DATALINK_PORT)
    parser.add_argument("--play", action="store_true", help="show the stream live in a low-latency ffplay window")
    parser.add_argument("--decode-ability", metavar="WxH",
                        help="announce the app's decode resolution (SendAppDecodeAbility, e.g. 1920x1080)")
    parser.add_argument("-v", "--verbose", action="store_true")
    raise SystemExit(run(parser.parse_args()))


if __name__ == "__main__":
    main()
