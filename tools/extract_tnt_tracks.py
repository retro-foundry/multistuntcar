#!/usr/bin/env python3
"""
Extract the hidden TNT track pack from SCR-TNT into 804-byte track blobs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import List


EXPECTED_SOURCE_SHA1 = "f6fd44dc425e367b3ce3c6af18cf07f2d7a50d7b"

PIECE_DATA_OFFSETS_SIGNATURE = bytes.fromhex(
    "50 b2 a3 b2 a9 ff fe b2 59 b3 20 da d8 b3 3b b4 "
    "0a 46 2e 20 9e b4 a9 80 85 2e 60 a5 30 c9 81 90"
)

TRACK_NAMES = [
    "DizzyDescent",
    "WittyWay",
    "CrazyCaper",
    "AmazingAdept",
    "JerkilyJump",
    "EvillyEpisode",
    "TeasingTemper",
    "RatRace",
]

# Hidden track starts inside SCR-TNT code hunk.
RAW_TRACK_STARTS = [0x03BE, 0x04BE, 0x0563, 0x067C, 0x074F, 0x0807, 0x0926, 0x09F3]

MAX_PIECES_PER_TRACK = 100
TRACK_DATA_SIZE = 804
TAB_5B2C4 = [3, 4, 4, 3]


def _read_u16_be(buf: bytes, offset: int) -> int:
    return (buf[offset] << 8) | buf[offset + 1]


def _decode_ptr_word(base_offset: int, word: int) -> int:
    """
    Emulate 68000 decode used by set.road.data1:
      rol.w #8; sub.w #$b100; andi #$ffff; addi #piece.data.offsets
    """
    rotated = ((word >> 8) | ((word & 0xFF) << 8)) & 0xFFFF
    return base_offset + ((rotated - 0xB100) & 0xFFFF)


def _srd1_sub3(d0: int, factor1: int) -> int:
    d0 &= 0xFF
    factor1 &= 0xFF
    if factor1 & 0x80:
        return (d0 - (1 if (factor1 & 0x40) else 16)) & 0xFF
    return (d0 + (1 if (factor1 & 0x40) else 16)) & 0xFF


def _decode_hidden_track(
    data: bytes,
    base_offset: int,
    piece_offsets_table: bytes,
    y_offsets_table: bytes,
    raw_start: int,
) -> bytes:
    cursor = 0

    def get_byte() -> int:
        nonlocal cursor
        pos = raw_start + cursor
        if pos >= len(data):
            raise ValueError("Unexpected EOF while decoding raw track block")
        value = data[pos]
        cursor += 1
        return value

    num_sections = get_byte()
    player_start = get_byte()
    _near_start_line = get_byte()
    _half_lap = get_byte()

    # Stored as bytes that become a word in near.x.coord (low/high byte layout in RAM).
    hi = get_byte()
    lo = get_byte()
    near_x = (lo << 8) | hi
    near_z = near_x

    other_road_line_colour = 0
    prompt_chars = 0
    last_factor = 0
    last_xz = 0

    road_xz = [0] * MAX_PIECES_PER_TRACK
    road_angle_piece = [0] * MAX_PIECES_PER_TRACK
    left_y_ids = [0] * MAX_PIECES_PER_TRACK
    right_y_ids = [0] * MAX_PIECES_PER_TRACK
    left_shifts = [0] * MAX_PIECES_PER_TRACK
    right_shifts = [0] * MAX_PIECES_PER_TRACK

    section = 0
    while True:
        if prompt_chars:
            prompt_chars = (prompt_chars - 1) & 0xFF
            factor1 = last_factor
            road_angle_piece[section] = factor1
            if factor1 & 0x10:
                factor1 ^= 0xC0
            xz_value = _srd1_sub3(last_xz, factor1)
        else:
            factor1 = get_byte()
            road_angle_piece[section] = factor1
            if (factor1 & 0x0F) == 0x0F:
                prompt_chars = (factor1 >> 4) & 0xFF
                continue
            last_factor = factor1
            xz_value = get_byte()

        road_xz[section] = xz_value
        last_xz = xz_value

        line_value = 0x80 if (other_road_line_colour == 2) else 0x00

        template_low = factor1 & 0x0F
        if template_low >= 12:
            road_angle_piece[section] &= 0xF0
            left_val = TAB_5B2C4[template_low - 12] if 0 <= template_low - 12 < len(TAB_5B2C4) else 0
            right_source = TAB_5B2C4[template_low - 10] if 0 <= template_low - 10 < len(TAB_5B2C4) else 0
            left_y_ids[section] = left_val
        else:
            left_val = get_byte()
            left_y_ids[section] = left_val
            right_source = left_val if (factor1 & 0x20) else get_byte()

        right_y_ids[section] = ((right_source & 0x7F) | line_value) & 0xFF

        # fetch.near.section.stuff
        left_id = left_y_ids[section] & 0xFF
        y_words_flag = left_id
        left_off_word = _read_u16_be(y_offsets_table, ((left_id << 1) & 0xFF))

        right_id = right_y_ids[section] & 0xFF
        other_road_line_colour = 2 if (right_id & 0x80) else 0
        right_off_word = _read_u16_be(y_offsets_table, ((right_id << 1) & 0xFF))

        piece_template = road_angle_piece[section] & 0x0F
        piece_word = _read_u16_be(piece_offsets_table, (piece_template * 2) & 0x1F)
        piece_ptr = _decode_ptr_word(base_offset, piece_word)

        piece_d2 = data[piece_ptr]
        num_coords = data[piece_ptr + piece_d2]
        num_segments = ((num_coords >> 1) - 1) & 0xFF

        def read_y(offset_word: int, is_word_flag: int, segment: int) -> int:
            ptr = _decode_ptr_word(base_offset, offset_word)
            if is_word_flag & 0x80:
                i = (segment * 2) & 0xFF
                return (((data[ptr + i] & 0x7F) << 8) | data[ptr + i + 1]) & 0xFFFF
            packed = data[ptr + segment]
            return (((packed & 0x0F) << 8) | ((packed << 1) & 0xE0)) & 0xFFFF

        # IMPORTANT: this mirrors the existing port conversion behavior:
        # right side shifts use the left-side "word/byte storage" flag.
        dy = read_y(left_off_word, y_words_flag, 0)
        near_x = (near_x - dy) & 0xFFFF
        left_shifts[section] = near_x

        dy = read_y(left_off_word, y_words_flag, num_segments)
        near_x = (near_x + dy) & 0xFFFF

        dy = read_y(right_off_word, y_words_flag, 0)
        near_z = (near_z - dy) & 0xFFFF
        right_shifts[section] = near_z

        dy = read_y(right_off_word, y_words_flag, num_segments)
        near_z = (near_z + dy) & 0xFFFF

        other_road_line_colour = (other_road_line_colour + ((num_coords - 2) & 0xFF)) & 2

        section += 1
        if section == num_sections:
            break

    # B.1ca2a..B.1ca2f
    extra = [get_byte() for _ in range(6)]
    standard_boost = extra[2] & 0xFF
    super_boost = extra[3] & 0xFF

    out = bytearray(TRACK_DATA_SIZE)
    out[0] = num_sections & 0xFF
    out[1] = player_start & 0xFF
    out[2:102] = bytes(road_xz)
    out[102:202] = bytes(road_angle_piece)
    out[202:302] = bytes(left_y_ids)
    out[302:402] = bytes(right_y_ids)

    p = 402
    for value in left_shifts:
        out[p] = (value >> 8) & 0xFF
        out[p + 1] = value & 0xFF
        p += 2
    for value in right_shifts:
        out[p] = (value >> 8) & 0xFF
        out[p + 1] = value & 0xFF
        p += 2

    out[802] = standard_boost
    out[803] = super_boost
    return bytes(out)


def _sha1_hex(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def _sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Extract hidden TNT tracks from SCR-TNT")
    parser.add_argument("--input", default="reference/SCR-TNT", help="Path to SCR-TNT binary")
    parser.add_argument("--output-dir", default="data/Tracks/TNT", help="Directory for output .bin files")
    parser.add_argument(
        "--manifest",
        default=None,
        help="Manifest JSON path (default: <output-dir>/manifest.json)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Skip source SHA1 validation",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    input_path = Path(args.input)
    output_dir = Path(args.output_dir)
    manifest_path = Path(args.manifest) if args.manifest else output_dir / "manifest.json"

    source_data = input_path.read_bytes()
    source_sha1 = _sha1_hex(source_data)

    if not args.force and source_sha1 != EXPECTED_SOURCE_SHA1:
        raise SystemExit(
            f"Source SHA1 mismatch for {input_path}: got {source_sha1}, expected {EXPECTED_SOURCE_SHA1}. "
            "Use --force to bypass."
        )

    base_offset = source_data.find(PIECE_DATA_OFFSETS_SIGNATURE)
    if base_offset < 0:
        raise SystemExit("Could not locate piece.data.offsets signature in source binary.")

    piece_offsets_table = source_data[base_offset : base_offset + 32]
    y_offsets_table = source_data[base_offset + 32 : base_offset + 288]

    output_dir.mkdir(parents=True, exist_ok=True)

    manifest_tracks: List[dict] = []
    for index, (name, raw_start) in enumerate(zip(TRACK_NAMES, RAW_TRACK_STARTS)):
        decoded = _decode_hidden_track(
            source_data,
            base_offset,
            piece_offsets_table,
            y_offsets_table,
            raw_start,
        )
        if len(decoded) != TRACK_DATA_SIZE:
            raise SystemExit(f"Decoded track {name} has unexpected size {len(decoded)}")

        out_path = output_dir / f"{name}.bin"
        out_path.write_bytes(decoded)

        manifest_tracks.append(
            {
                "index": index,
                "name": name,
                "file": out_path.name,
                "raw_start_hex": f"0x{raw_start:04X}",
                "size": len(decoded),
                "sha1": _sha1_hex(decoded),
                "sha256": _sha256_hex(decoded),
            }
        )

    manifest = {
        "source_file": str(input_path.as_posix()),
        "source_sha1": source_sha1,
        "expected_source_sha1": EXPECTED_SOURCE_SHA1,
        "piece_data_offsets_base_hex": f"0x{base_offset:06X}",
        "raw_block_starts_hex": [f"0x{value:04X}" for value in RAW_TRACK_STARTS],
        "tracks": manifest_tracks,
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"Extracted {len(manifest_tracks)} tracks to {output_dir}")
    print(f"Wrote manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
