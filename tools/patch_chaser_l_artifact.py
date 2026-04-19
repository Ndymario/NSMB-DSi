#!/usr/bin/env python3
"""One-off fixer for the Chaser_L NSBTX top-left artifact.

Patches texel (13,12) to transparent index 0 in frames 0000 and 0005.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Iterable

import ndspy.texture as nd_texture


TARGET_FRAMES = {"0000", "0005"}
TARGET_X = 13
TARGET_Y = 12


def i4_offset_and_nibble(width: int, height: int, x: int, y: int) -> tuple[int, bool]:
    if width % 8 != 0 or height % 8 != 0:
        raise ValueError(f"I4 texture must be tiled in 8x8 blocks, got {width}x{height}")
    if x < 0 or y < 0 or x >= width or y >= height:
        raise ValueError(f"Texel ({x},{y}) is outside texture bounds {width}x{height}")

    tiles_x = width // 8
    tile_x, in_tile_x = divmod(x, 8)
    tile_y, in_tile_y = divmod(y, 8)
    tile_index = tile_y * tiles_x + tile_x
    tile_base = tile_index * 32  # I4 = 32 bytes per 8x8 tile.
    byte_offset = tile_base + (in_tile_y * 4) + (in_tile_x // 2)
    high_nibble = (in_tile_x & 1) == 1
    return byte_offset, high_nibble


def read_i4_texel(texture: nd_texture.Texture, x: int, y: int) -> int:
    byte_offset, high_nibble = i4_offset_and_nibble(texture.width, texture.height, x, y)
    value = texture.data1[byte_offset]
    return (value >> 4) & 0xF if high_nibble else value & 0xF


def clear_i4_texel(texture: nd_texture.Texture, x: int, y: int) -> tuple[int, int]:
    if texture.format != nd_texture.TextureFormat.I4:
        raise ValueError(f"Expected I4 texture format, got {texture.format}")

    byte_offset, high_nibble = i4_offset_and_nibble(texture.width, texture.height, x, y)
    mutable = bytearray(texture.data1)
    before = ((mutable[byte_offset] >> 4) & 0xF) if high_nibble else (mutable[byte_offset] & 0xF)

    if high_nibble:
        mutable[byte_offset] &= 0x0F
    else:
        mutable[byte_offset] &= 0xF0

    texture.data1 = bytes(mutable)
    after = read_i4_texel(texture, x, y)
    return before, after


def patch_frames(nsbtx: nd_texture.NSBTX, frame_names: Iterable[str]) -> list[tuple[str, int, int]]:
    frame_set = set(frame_names)
    patched: list[tuple[str, int, int]] = []

    for name, tex in nsbtx.textures:
        if name not in frame_set:
            continue
        before, after = clear_i4_texel(tex, TARGET_X, TARGET_Y)
        patched.append((name, before, after))
        frame_set.remove(name)

    if frame_set:
        missing = ", ".join(sorted(frame_set))
        raise RuntimeError(f"Missing expected frame names: {missing}")

    return patched


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_nsbtx", type=Path, help="Input Chaser_L.nsbtx path")
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Optional output path (default: in-place overwrite)",
    )
    args = parser.parse_args()

    input_path: Path = args.input_nsbtx
    output_path: Path = args.output if args.output is not None else input_path

    original_bytes = input_path.read_bytes()
    nsbtx = nd_texture.NSBTX(original_bytes)
    original_size = len(original_bytes)

    print(
        f"[patch] input={input_path} size={original_size} "
        f"textures={len(nsbtx.textures)} palettes={len(nsbtx.palettes)}"
    )

    patched = patch_frames(nsbtx, TARGET_FRAMES)
    for name, before, after in patched:
        print(f"[patch] frame={name} texel=({TARGET_X},{TARGET_Y}) {before} -> {after}")
        if after != 0:
            raise RuntimeError(f"Patch failed for frame {name}; expected value 0, got {after}")

    out_bytes = nsbtx.save()
    output_path.write_bytes(out_bytes)

    reparsed = nd_texture.NSBTX(output_path.read_bytes())
    if len(reparsed.textures) != len(nsbtx.textures) or len(reparsed.palettes) != len(nsbtx.palettes):
        raise RuntimeError("Structural mismatch after save/reload")

    for name, tex in reparsed.textures:
        if name in TARGET_FRAMES:
            value = read_i4_texel(tex, TARGET_X, TARGET_Y)
            if value != 0:
                raise RuntimeError(f"Verification failed for frame {name}; texel value is {value}")

    print(f"[patch] output={output_path} size={len(out_bytes)}")
    if len(out_bytes) != original_size:
        print(f"[patch] warning: size changed ({original_size} -> {len(out_bytes)})")
    print("[patch] complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
