#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def crc16_a001(data: bytes, init: int = 0xFFFF) -> int:
    crc = init
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def parse_u32(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "little")


def parse_u16(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 2], "little")


def dump_fields(buf: bytes) -> None:
    unitcode = buf[0x12]
    dsi_flags_1c = buf[0x1C]
    title_flags_1bf = buf[0x1BF]
    arm9i_off = parse_u32(buf, 0x1C0)
    arm9i_ram = parse_u32(buf, 0x1C8)
    arm9i_size = parse_u32(buf, 0x1CC)
    arm7i_off = parse_u32(buf, 0x1D0)
    arm7i_ram = parse_u32(buf, 0x1D8)
    arm7i_size = parse_u32(buf, 0x1DC)
    rsa_sig = buf[0xF80:0x1000]
    has_rsa = any(x != 0 for x in rsa_sig)

    hdr_crc_stored = parse_u16(buf, 0x15E)
    hdr_crc_calc = crc16_a001(buf[:0x15E])

    print(f"unitcode[0x12]: 0x{unitcode:02X}")
    print(f"flags[0x1C]:   0x{dsi_flags_1c:02X}")
    print(f"flags[0x1BF]:  0x{title_flags_1bf:02X}")
    print(f"arm9i: off=0x{arm9i_off:08X} ram=0x{arm9i_ram:08X} size=0x{arm9i_size:08X}")
    print(f"arm7i: off=0x{arm7i_off:08X} ram=0x{arm7i_ram:08X} size=0x{arm7i_size:08X}")
    print(f"rsa_sig[F80-FFF]: {'present' if has_rsa else 'all-zero'}")
    print(f"header_crc[0x15E]: stored=0x{hdr_crc_stored:04X} calc=0x{hdr_crc_calc:04X}")

    if (unitcode & 0x02) == 0:
        print("mode: NDS-only header (will boot in NDS compatibility mode)")
    else:
        print("mode: DSi-capable unitcode bit is set")

    if arm9i_size == 0 and arm7i_size == 0:
        print("twl payload: no ARM9i/ARM7i payload declared")
    else:
        print("twl payload: ARM9i/ARM7i payload entries are non-zero")

    if not has_rsa:
        print("note: DSi RSA signature block appears empty")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Inspect and optionally patch NDS/DSi cartridge header fields."
    )
    parser.add_argument("rom", type=Path, help="Path to .nds/.srl ROM")
    parser.add_argument(
        "--set-unitcode",
        type=lambda v: int(v, 0),
        help="Set header unitcode byte (0x12), e.g. 0x02 for NDS+DSi",
    )
    parser.add_argument(
        "--set-1c",
        type=lambda v: int(v, 0),
        help="Set header byte 0x1C directly (DSi flags byte)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Output ROM path for patched result (defaults to in-place if omitted)",
    )
    args = parser.parse_args()

    data = bytearray(args.rom.read_bytes())
    if len(data) < 0x1000:
        raise SystemExit("ROM is too small to contain full DSi header region (0x1000 bytes).")

    print(f"== Header before: {args.rom}")
    dump_fields(bytes(data[:0x1000]))

    changed = False
    if args.set_unitcode is not None:
        if args.set_unitcode < 0 or args.set_unitcode > 0xFF:
            raise SystemExit("--set-unitcode must fit in one byte.")
        data[0x12] = args.set_unitcode & 0xFF
        changed = True

    if args.set_1c is not None:
        if args.set_1c < 0 or args.set_1c > 0xFF:
            raise SystemExit("--set-1c must fit in one byte.")
        data[0x1C] = args.set_1c & 0xFF
        changed = True

    if changed:
        new_crc = crc16_a001(bytes(data[:0x15E]))
        data[0x15E:0x160] = new_crc.to_bytes(2, "little")
        out_path = args.output if args.output is not None else args.rom
        out_path.write_bytes(data)
        print(f"\nPatched header written to: {out_path}")
        print("Recomputed header checksum at 0x15E.")
        print(f"\n== Header after: {out_path}")
        dump_fields(bytes(data[:0x1000]))


if __name__ == "__main__":
    main()
