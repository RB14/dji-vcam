"""Checks the DUML codec against frames captured from DJI Mimo and published by osmosis."""

import unittest

import duml

# (hex frame, description). The first two are real Mimo captures.
KNOWN_FRAMES = [
    "550f04a202f01bcb40002b04009ab9",       # 00/2b session open [04 00] (Mimo)
    "55110492021c1dcb40531000000000894a",   # 53/10 wake (Mimo)
    "550d043302070780400707fbcd",           # 07/07 GetWifiSsid
    "550d043302070e8040070e5e01",           # 07/0e GetWifiPassword
    "553304c202079280400745203238346165356238643736623333373561303461363431376164373162656133046f736d6fa0b4",
]


class DumlTest(unittest.TestCase):
    def test_roundtrip_known_frames(self) -> None:
        for hex_frame in KNOWN_FRAMES:
            raw = bytes.fromhex(hex_frame)
            frame = duml.decode(raw)
            self.assertEqual(frame.encode(), raw, hex_frame)

    def test_build_get_ssid(self) -> None:
        frame = duml.Frame(duml.ADDR_APP, duml.ADDR_WIFI, 0x8007, duml.FLAG_REQUEST, 0x07, 0x07)
        self.assertEqual(frame.encode().hex(), "550d043302070780400707fbcd")

    def test_build_pairing(self) -> None:
        payload = duml.pack_string("284ae5b8d76b3375a04a6417ad71bea3") + duml.pack_string("osmo")
        frame = duml.Frame(duml.ADDR_APP, duml.ADDR_WIFI, 0x8092, duml.FLAG_REQUEST, 0x07, 0x45, payload)
        self.assertEqual(frame.encode().hex(), KNOWN_FRAMES[4])

    def test_reply_swaps_addresses(self) -> None:
        request = duml.Frame(duml.ADDR_WIFI, duml.ADDR_APP, 0x1234, duml.FLAG_REQUEST, 0x07, 0x46, b"\x01")
        self.assertEqual(request.reply(b"\x01").encode().hex(), "550e046602073412c0074601a45c")

    def test_stream_parser_handles_split_and_junk(self) -> None:
        a = bytes.fromhex(KNOWN_FRAMES[2])
        b = bytes.fromhex(KNOWN_FRAMES[3])
        parser = duml.StreamParser()
        stream = b"\x00\x13" + a + b
        frames = parser.feed(stream[:7]) + parser.feed(stream[7:20]) + parser.feed(stream[20:])
        self.assertEqual([f.cmd_id for f in frames], [0x07, 0x0E])


if __name__ == "__main__":
    unittest.main()
