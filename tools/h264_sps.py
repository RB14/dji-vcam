"""Minimal H.264 SPS parser: enough to read the coded picture size and frame-rate info."""

from __future__ import annotations

from dataclasses import dataclass


class BitReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0

    def bit(self) -> int:
        byte = self.data[self.pos >> 3]
        value = (byte >> (7 - (self.pos & 7))) & 1
        self.pos += 1
        return value

    def bits(self, count: int) -> int:
        value = 0
        for _ in range(count):
            value = (value << 1) | self.bit()
        return value

    def ue(self) -> int:
        zeros = 0
        while self.bit() == 0:
            zeros += 1
        return (1 << zeros) - 1 + self.bits(zeros)

    def se(self) -> int:
        value = self.ue()
        return (value + 1) // 2 if value & 1 else -(value // 2)


def strip_emulation_prevention(nal: bytes) -> bytes:
    out = bytearray()
    zeros = 0
    for byte in nal:
        if zeros >= 2 and byte == 0x03:
            zeros = 0
            continue
        out.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    return bytes(out)


def _skip_scaling_list(reader: BitReader, size: int) -> None:
    last = nxt = 8
    for _ in range(size):
        if nxt != 0:
            nxt = (last + reader.se() + 256) % 256
        last = nxt if nxt != 0 else last


@dataclass(frozen=True)
class SpsInfo:
    profile: int
    level: int
    width: int
    height: int
    fps: float | None  # from VUI timing info, if present

    def __str__(self) -> str:
        rate = f" @ {self.fps:g} fps" if self.fps else ""
        return f"{self.width}x{self.height}{rate} (profile {self.profile}, level {self.level / 10:g})"


def parse_sps(nal: bytes) -> SpsInfo:
    """`nal` starts with the NAL header byte (type 7)."""
    r = BitReader(strip_emulation_prevention(nal[1:]))
    profile = r.bits(8)
    r.bits(8)  # constraint flags
    level = r.bits(8)
    r.ue()  # seq_parameter_set_id
    chroma_format = 1
    if profile in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135):
        chroma_format = r.ue()
        if chroma_format == 3:
            r.bit()  # separate_colour_plane_flag
        r.ue()  # bit_depth_luma_minus8
        r.ue()  # bit_depth_chroma_minus8
        r.bit()  # qpprime_y_zero_transform_bypass_flag
        if r.bit():  # seq_scaling_matrix_present_flag
            for i in range(8 if chroma_format != 3 else 12):
                if r.bit():
                    _skip_scaling_list(r, 16 if i < 6 else 64)
    r.ue()  # log2_max_frame_num_minus4
    poc_type = r.ue()
    if poc_type == 0:
        r.ue()
    elif poc_type == 1:
        r.bit()
        r.se()
        r.se()
        for _ in range(r.ue()):
            r.se()
    r.ue()  # max_num_ref_frames
    r.bit()  # gaps_in_frame_num_value_allowed_flag
    width_mbs = r.ue() + 1
    height_units = r.ue() + 1
    frame_mbs_only = r.bit()
    if not frame_mbs_only:
        r.bit()  # mb_adaptive_frame_field_flag
    r.bit()  # direct_8x8_inference_flag
    width = width_mbs * 16
    height = height_units * 16 * (2 - frame_mbs_only)
    if r.bit():  # frame_cropping_flag
        crop_x = 2 if chroma_format in (1, 2) else 1
        crop_y = (2 if chroma_format == 1 else 1) * (2 - frame_mbs_only)
        left, right, top, bottom = r.ue(), r.ue(), r.ue(), r.ue()
        width -= (left + right) * crop_x
        height -= (top + bottom) * crop_y
    fps = None
    try:
        if r.bit():  # vui_parameters_present_flag
            if r.bit():  # aspect_ratio_info_present_flag
                if r.bits(8) == 255:
                    r.bits(32)
            if r.bit():  # overscan_info_present_flag
                r.bit()
            if r.bit():  # video_signal_type_present_flag
                r.bits(4)
                if r.bit():
                    r.bits(24)
            if r.bit():  # chroma_loc_info_present_flag
                r.ue()
                r.ue()
            if r.bit():  # timing_info_present_flag
                num_units = r.bits(32)
                time_scale = r.bits(32)
                if num_units:
                    fps = time_scale / (2 * num_units)
    except IndexError:
        pass
    return SpsInfo(profile, level, width, height, fps)
