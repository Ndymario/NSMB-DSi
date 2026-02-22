#!/usr/bin/env python3

import argparse
import pathlib
import struct


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate a minimal test mod overlay payload binary."
    )
    parser.add_argument(
        "--out",
        default="build/mod_overlay_payload.bin",
        help="Output payload path (raw payload, not modovl.bin).",
    )
    parser.add_argument(
        "--exports-offset",
        default=0x20,
        type=lambda value: int(value, 0),
        help="Offset of ModOverlayExports inside payload image.",
    )
    args = parser.parse_args()

    # Entry stub (ARM state):
    #   mov r0, #1   ; return true
    #   bx  lr
    entry_stub = struct.pack("<II", 0xE3A00001, 0xE12FFF1E)

    exports_offset = args.exports_offset
    if exports_offset < len(entry_stub):
        raise ValueError("exports-offset must be >= entry stub size")
    if exports_offset & 0x3:
        raise ValueError("exports-offset must be 4-byte aligned")

    payload = bytearray(entry_stub)
    payload.extend(b"\x00" * (exports_offset - len(payload)))

    # struct ModOverlayExports
    # abi_version = 1
    # flags = 0
    # spawn_custom = nullptr (runtime will safely fall back to vanilla spawn)
    # on_scene_change = nullptr
    # shutdown = nullptr
    # reserved[3] = 0
    exports = struct.pack(
        "<IIIII3I",
        1,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    )
    payload.extend(exports)

    out_path = pathlib.Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(payload)

    print(f"wrote payload: {out_path} ({len(payload)} bytes)")
    print("entry_offset=0x0")
    print(f"exports_offset=0x{exports_offset:08x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
