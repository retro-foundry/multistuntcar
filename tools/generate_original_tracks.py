#!/usr/bin/env python3
"""
Generate the repo's original one-off track pack.

The game consumes 804-byte track blobs:
  count, start piece, 100 x/z bytes, 100 angle/template bytes,
  100 left y ids, 100 right y ids, 100 left shifts, 100 right shifts,
  standard boost, super boost.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Dict, List, Sequence, Tuple


MAX_PIECES_PER_TRACK = 100
TRACK_DATA_SIZE = 804

TRACK_NAME = "Skyline Spiral"
TRACK_FILE = "SkylineSpiral.bin"
START_PIECE = 0
STANDARD_BOOST = 42
SUPER_BOOST = 48

Vec = Tuple[int, int]
Coord = Tuple[int, int]
YProfile = Tuple[int, int, int, int]

AP_BY_TURN: Dict[Tuple[Vec, Vec], int] = {
    ((1, 0), (1, 0)): 0x40,
    ((1, 0), (1, 1)): 0x47,
    ((1, 1), (0, 1)): 0x96,
    ((0, 1), (0, 1)): 0x00,
    ((0, 1), (-1, 1)): 0x07,
    ((-1, 1), (-1, 0)): 0x56,
    ((-1, 0), (-1, 0)): 0xC0,
    ((-1, 0), (-1, 1)): 0xC6,
    ((-1, 1), (0, 1)): 0x97,
    ((-1, 0), (-1, -1)): 0xC7,
    ((-1, -1), (0, -1)): 0x16,
    ((0, -1), (0, -1)): 0x80,
    ((0, -1), (-1, -1)): 0x86,
    ((-1, -1), (-1, 0)): 0x57,
    ((0, -1), (1, -1)): 0x87,
    ((1, -1), (1, 0)): 0xD6,
}


# Launch-ramp and corner profiles borrowed from the original palette of y-section
# ids, arranged into a new route. Tuples are left id, right id, left shift, right shift.
LAUNCH_TOP: Sequence[YProfile] = (
    (0x3A, 0x3A, 1344, 1344),
    (0x7A, 0x7A, 2112, 2112),
    (0x1C, 0x1C, 4672, 4672),
    (0x1D, 0x1D, 6848, 6848),
    (0x1E, 0x1E, 8000, 8000),
    (0x1F, 0x1F, 7040, 7040),
    (0x22, 0x22, 4992, 4992),
    (0x27, 0x27, 2432, 2432),
    (0x43, 0x43, 1344, 1344),
)

RIGHT_SIDE_STUTTERS: Sequence[YProfile] = (
    (0x00, 0x00, 1344, 1344),
    (0x48, 0x48, 832, 832),
    (0x00, 0x00, 832, 832),
    (0x39, 0x39, 832, 832),
    (0x00, 0x00, 1536, 1536),
    (0x48, 0x48, 1024, 1024),
)

STRAIGHT_SKYLINE_PROFILES: Sequence[YProfile] = (
    (0x6A, 0x6A, 1600, 1600),
    (0x6B, 0x6B, 1600, 1600),
    (0x24, 0x24, 2880, 2880),
    (0x50, 0x50, 3840, 3840),
    (0x50, 0x50, 5600, 5600),
    (0x25, 0x25, 7040, 7040),
    (0x19, 0x19, 7200, 7200),
    (0x63, 0x63, 6400, 6400),
)


def skyline_spiral_route() -> List[Coord]:
    """A 46-piece route with an outside launch, inner switchbacks, and a climb home."""

    route: List[Coord] = []
    route.extend((x, 0) for x in range(3, 15))
    route.append((15, 1))
    route.extend((15, z) for z in range(2, 8))
    route.extend(
        [
            (14, 8),
            (13, 8),
            (12, 8),
            (11, 9),
            (11, 10),
            (11, 11),
            (11, 12),
            (10, 13),
            (9, 13),
            (8, 13),
            (7, 12),
            (7, 11),
            (7, 10),
            (7, 9),
            (6, 8),
            (5, 8),
            (4, 8),
            (3, 8),
            (2, 8),
            (1, 7),
            (1, 6),
            (1, 5),
            (1, 4),
            (1, 3),
            (1, 2),
            (1, 1),
            (2, 0),
        ]
    )
    return route


def delta(a: Coord, b: Coord) -> Vec:
    return b[0] - a[0], b[1] - a[1]


def validate_route(route: Sequence[Coord]) -> None:
    if len(route) > MAX_PIECES_PER_TRACK:
        raise ValueError(f"Track has {len(route)} pieces, max is {MAX_PIECES_PER_TRACK}")
    if len(set(route)) != len(route):
        raise ValueError("Track route reuses a grid cube")
    for x, z in route:
        if not (0 <= x < 16 and 0 <= z < 16):
            raise ValueError(f"Track coordinate outside 16x16 grid: {(x, z)}")
    for index, coord in enumerate(route):
        prev_coord = route[index - 1]
        next_coord = route[(index + 1) % len(route)]
        turn = (delta(prev_coord, coord), delta(coord, next_coord))
        if turn not in AP_BY_TURN:
            raise ValueError(f"No angle/template mapping for piece {index} turn {turn}")


def build_angle_templates(route: Sequence[Coord]) -> List[int]:
    values: List[int] = []
    for index, coord in enumerate(route):
        turn = (delta(route[index - 1], coord), delta(coord, route[(index + 1) % len(route)]))
        values.append(AP_BY_TURN[turn])
    return values


def build_y_profiles(angle_templates: Sequence[int]) -> List[YProfile]:
    profiles: List[YProfile] = [(0x00, 0x00, 1280, 1280) for _ in angle_templates]

    for index, profile in enumerate(LAUNCH_TOP):
        profiles[index] = profile

    # One extra straight before the top-right corner lets the launch settle.
    profiles[9] = (0x00, 0x00, 1344, 1344)
    profiles[10] = (0x04, 0x03, 832, 1344)
    profiles[11] = (0x17, 0x16, 736, 1856)
    profiles[12] = (0x1A, 0x9B, 736, 1856)
    profiles[13] = (0x03, 0x04, 832, 1344)

    for offset, profile in enumerate(RIGHT_SIDE_STUTTERS, start=14):
        profiles[offset] = profile

    skyline_index = 0
    for index in range(20, 31):
        template = angle_templates[index] & 0x0F
        height = 3600 + (index - 20) * 320
        profiles[index] = (0x00, 0x00, height, height)
        if template == 0 and skyline_index < len(STRAIGHT_SKYLINE_PROFILES):
            profiles[index] = STRAIGHT_SKYLINE_PROFILES[skyline_index]
            skyline_index += 1

    # Bring the return leg down in stages so the final north-to-east corner is sane.
    descent_start = 31
    descent_end = len(angle_templates) - 1
    for index in range(descent_start, len(angle_templates)):
        t_num = index - descent_start
        t_den = max(1, descent_end - descent_start)
        height = 5120 - ((5120 - 1280) * t_num) // t_den
        profiles[index] = (0x00, 0x00, height, height)

    return profiles


def put_word(out: bytearray, offset: int, value: int) -> None:
    value &= 0xFFFF
    out[offset] = (value >> 8) & 0xFF
    out[offset + 1] = value & 0xFF


def encode_track(route: Sequence[Coord]) -> bytes:
    validate_route(route)

    angle_templates = build_angle_templates(route)
    y_profiles = build_y_profiles(angle_templates)

    out = bytearray(TRACK_DATA_SIZE)
    out[0] = len(route)
    out[1] = START_PIECE

    for index, (x, z) in enumerate(route):
        out[2 + index] = ((z & 0x0F) << 4) | (x & 0x0F)

    for index, value in enumerate(angle_templates):
        out[102 + index] = value & 0xFF

    for index, (left_id, right_id, _left_shift, _right_shift) in enumerate(y_profiles):
        out[202 + index] = left_id & 0xFF
        out[302 + index] = right_id & 0xFF

    for index, (_left_id, _right_id, left_shift, right_shift) in enumerate(y_profiles):
        put_word(out, 402 + index * 2, left_shift)
        put_word(out, 602 + index * 2, right_shift)

    out[802] = STANDARD_BOOST
    out[803] = SUPER_BOOST
    return bytes(out)


def sha1_hex(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate the Original track pack")
    parser.add_argument("--output-dir", default="data/Tracks/Original", help="Directory for generated track files")
    parser.add_argument("--manifest", default=None, help="Manifest path; defaults to <output-dir>/manifest.json")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output_dir = Path(args.output_dir)
    manifest_path = Path(args.manifest) if args.manifest else output_dir / "manifest.json"
    output_dir.mkdir(parents=True, exist_ok=True)

    route = skyline_spiral_route()
    data = encode_track(route)
    out_path = output_dir / TRACK_FILE
    out_path.write_bytes(data)

    manifest = {
        "tracks": [
            {
                "index": 0,
                "name": TRACK_NAME,
                "file": TRACK_FILE,
                "size": len(data),
                "pieces": len(route),
                "start_piece": START_PIECE,
                "standard_boost": STANDARD_BOOST,
                "super_boost": SUPER_BOOST,
                "sha1": sha1_hex(data),
                "sha256": sha256_hex(data),
                "route": [{"x": x, "z": z} for x, z in route],
                "design": "Outside launch ramp, right-side stutters, elevated inner switchbacks, staged descent home.",
            }
        ]
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"Generated {TRACK_NAME}: {out_path} ({len(data)} bytes, {len(route)} pieces)")
    print(f"Wrote manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
