"""Checks the SPS parser against the live-view SPS captured from the Action 5 Pro."""

import unittest

from h264_sps import parse_sps

# 1280x720 High@3.2, as identified by ffprobe on the same capture.
ACTION5PRO_LIVEVIEW_SPS = bytes.fromhex("67640020acb402802dd3501010106d0a1350")


class SpsTest(unittest.TestCase):
    def test_liveview_sps(self) -> None:
        info = parse_sps(ACTION5PRO_LIVEVIEW_SPS)
        self.assertEqual((info.width, info.height), (1280, 720))
        self.assertEqual((info.profile, info.level), (100, 32))


if __name__ == "__main__":
    unittest.main()
