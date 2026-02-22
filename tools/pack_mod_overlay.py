#!/usr/bin/env python3

import argparse
import pathlib
import struct
import zlib


MODOVL_MAGIC = 0x4D4F4456  # "MODV"
HEADER_FMT = "<IHHIIIIII"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
MAX_IMAGE_SIZE = 0x00100000  # 1 MB


def parse_int(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description="Pack mod overlay payload into modovl.bin")
    parser.add_argument("--payload", required=True, help="Path to raw overlay payload binary")
    parser.add_argument("--out", required=True, help="Output modovl.bin path")
    parser.add_argument(
        "--entry-offset",
        default="0",
        type=parse_int,
        help="Entry offset relative to payload start",
    )
    parser.add_argument(
        "--exports-offset",
        default="0",
        type=parse_int,
        help="Exports table offset relative to payload start",
    )
    parser.add_argument("--bss-size", default="0", type=parse_int, help="BSS size to reserve")
    parser.add_argument("--abi-version", default=1, type=int, help="Overlay ABI version")
    args = parser.parse_args()

    payload_path = pathlib.Path(args.payload)
    out_path = pathlib.Path(args.out)
    payload = payload_path.read_bytes()

    if len(payload) == 0:
        raise ValueError("Payload is empty")
    image_size = HEADER_SIZE + len(payload)
    if image_size > MAX_IMAGE_SIZE:
        raise ValueError(f"Image too large ({image_size} bytes > {MAX_IMAGE_SIZE} bytes)")
    if args.entry_offset < 0 or args.entry_offset >= len(payload):
        raise ValueError("entry-offset must point inside the payload")
    if args.exports_offset < 0 or args.exports_offset >= len(payload):
        raise ValueError("exports-offset must point inside the payload")

    # Runtime offsets are relative to the loaded image base (header at offset 0).
    entry_offset = HEADER_SIZE + args.entry_offset
    exports_offset = HEADER_SIZE + args.exports_offset

    image_crc = zlib.crc32(payload) & 0xFFFFFFFF

    header_wo_crc = struct.pack(
        HEADER_FMT,
        MODOVL_MAGIC,
        args.abi_version,
        HEADER_SIZE,
        image_size,
        args.bss_size,
        entry_offset,
        exports_offset,
        image_crc,
        0,
    )
    header_crc = zlib.crc32(header_wo_crc[: HEADER_SIZE - 4]) & 0xFFFFFFFF
    header = struct.pack(
        HEADER_FMT,
        MODOVL_MAGIC,
        args.abi_version,
        HEADER_SIZE,
        image_size,
        args.bss_size,
        entry_offset,
        exports_offset,
        image_crc,
        header_crc,
    )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(header + payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
