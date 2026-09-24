"""Checks the UDP datalink framing against a command packet derived from osmosis."""

import unittest

import datalink
import duml

# 0x00/0x88 register command: session 0x4c21, base 0x5a38, first command seq 0x5a40.
REGISTER_PACKET = ("2f80214c405a05dd385a405a0000000001010000"
                   "551b0475022800a0400088170046237c415050000000000002e6e8")


class DatalinkFramingTest(unittest.TestCase):
    def test_command_packet_matches_reference(self) -> None:
        frame = duml.Frame(duml.ADDR_APP, duml.ADDR_DM368_1, 0xA000, duml.FLAG_REQUEST, 0x00, 0x88,
                           bytes.fromhex("170046237c415050000000000002"))
        body = datalink.routing_header(0x5A40, 1) + frame.encode()
        packet = datalink.header(datalink.TYPE_COMMAND, len(body), 0x4C21, 0x5A40) + body
        self.assertEqual(packet.hex(), REGISTER_PACKET)

    def test_handshake_payload_is_40_bytes(self) -> None:
        self.assertEqual(len(datalink.HANDSHAKE_TAIL) + 2, 40)

    def test_header_xor(self) -> None:
        self.assertEqual(datalink.header(0x00, 40, 0x4C21, 0).hex(), "3080214c000000dd")


if __name__ == "__main__":
    unittest.main()
