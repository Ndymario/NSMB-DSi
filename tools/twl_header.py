#!/usr/bin/env python3
"""Turn an NTR (DS) ROM into a DSi-enhanced (TWL) one, in place.

NCPatcher only writes NTR header fields, so this runs as a post-build hook.
Field offsets and meanings are from GBATEK "DS Cartridge Header" and "DSi
Cartridge Header". The result is unsigned: Unlaunch, TWiLight Menu++ /
nds-bootstrap and emulators boot it in DSi mode; the stock DSi menu does not.

The game code stays the same, so saves, cheats and per-game loader settings
keep working. The module's code lives in the normal binaries and only needs the
launcher to leave the console in DSi mode, so the ARM9i/ARM7i binaries are
small inert stubs. They exist because TWiLight Menu++ refuses a DSi-enhanced
ROM whose ARM9i/ARM7i offsets are below 8000h or whose first 16 bytes are all
00h, all FFh or the same as at 8000h ("The DSi binaries are missing"; see
checkDsiBinaries() in its ndsheaderbanner.cpp).

nds-bootstrap (what TWiLight Menu++ uses to run games from the SD card) only
runs DSi-enhanced ROMs in DSi mode if they were built with the DSi SDK, which
NSMB was not. It does run plain DS ROMs in DSi mode when the game's "Run in"
setting is "DSi mode (forced)", so --ds-copy also writes the untouched DS ROM
for that.

Usage:
  twl_header.py ROM                   patch ROM in place
  twl_header.py ROM --ds-copy OUT     also save the ROM as it was (DS header) to OUT first
  twl_header.py --check ROM           verify ROM already has the expected DSi header
"""

import argparse
import struct
import sys

HEADER_CRC_END = 0x15E
TWL_ALIGN = 0x80000  # [090h]/[092h] are in 512KB units

# [1B8h] ARM7 SCFG_EXT7 bits the launcher takes from the header (bits 0,1,2,10,18,31).
# Keep the NITRO DMA/sound circuits the NTR SDK was written for, leave SD/MMC
# off, and keep SCFG/MBK access so the module can see and use DSi hardware.
SCFG_EXT7 = 1 << 31

# [1BFh] bit0 = touchscreen/sound controller mode. 0 keeps the TSC in NDS mode,
# which is the only mode the NTR ARM7 code can drive.
APP_FLAGS = 0x00

# [01Ch] bit0 "has TWL-exclusive region". Loaders (and melonDS direct boot)
# only treat the ARM9i/ARM7i entries as valid when it is set.
DSI_FLAGS = 0x01

# Dedicated ARM9i/ARM7i load areas (GBATEK's usual addresses). The launcher
# copies the stubs there before the game starts; nothing runs or reads them,
# and the module's heaps later reuse both areas.
ARM9I_RAM = 0x02400000
ARM7I_RAM = 0x02E80000
STUB_SIZE = 0x400


def stub(name: str) -> bytes:
    text = f"NSMB-DSi {name} stub: not executed, see tools/twl_header.py".encode()
    return text.ljust(STUB_SIZE, b"\0")

# [1B0h] region-free; [1B4h] no access to SD, NAND or AES keys.
REGION_FREE = 0xFFFFFFFF
ACCESS_CONTROL = 0

# [180h..1AFh] NWRAM. All of WRAM-A/B/C goes to the ARM9 as one 768KB window at
# 0x03000000; the ARM7 gets no window, so it sees exactly what it would in DS
# mode. Must match modules/dsi/include/dsi/nwram.hpp, which re-applies it at
# boot. MBK9 = 0 leaves the slot registers writable from ARM9 for that.
# [1AFh] WRAMCNT = 3: the old 32K shared WRAM all to ARM7, as in DS mode.
NWRAM_MBK1_5 = (0x8C888480, 0x8C888480, 0x9C989490, 0x8C888480, 0x9C989490)
NWRAM_MBK6_8_ARM9 = (
    (0 << 4) | (3 << 12) | (4 << 20),    # WRAM-A 0x03000000-0x0303FFFF
    (8 << 3) | (3 << 12) | (16 << 19),   # WRAM-B 0x03040000-0x0307FFFF
    (16 << 3) | (3 << 12) | (24 << 19),  # WRAM-C 0x03080000-0x030BFFFF
)
MBK_SETTINGS = (struct.pack("<5I", *NWRAM_MBK1_5) + struct.pack("<3I", *NWRAM_MBK6_8_ARM9)
                + bytes(12) + bytes(3) + bytes([0x03]))
assert len(MBK_SETTINGS) == 0x30


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def banner_size(rom: bytes, offset: int) -> int:
    if offset == 0:
        return 0
    version = struct.unpack_from("<H", rom, offset)[0]
    # GBATEK "DS Cartridge Icon/Title": v1 840h, v2 940h, v3 A40h, DSi 23C0h.
    return {1: 0x840, 2: 0x940, 3: 0xA40, 0x103: 0x23C0}.get(version, 0x840)


def expected_image(rom: bytes) -> tuple[bytearray, int, bytes]:
    """Returns the patched header, and where the stubs go and their bytes."""
    hdr = bytearray(rom[:0x1000])
    if hdr[0x012] not in (0x00, 0x02):
        raise SystemExit(f"error: unit code {hdr[0x012]:#04x} is neither NDS nor NDS+DSi")
    arm9_rom = struct.unpack_from("<I", hdr, 0x020)[0]
    if arm9_rom < 0x4000:
        raise SystemExit("error: ARM9 starts inside the 16K DSi header area")
    if any(rom[0x1000:0x4000]):
        raise SystemExit("error: ROM 1000h-3FFFh is not empty; the DSi header area is in use")

    used = struct.unpack_from("<I", hdr, 0x080)[0]
    twl_start = (used + TWL_ALIGN - 1) // TWL_ALIGN
    arm9i_rom = twl_start * TWL_ALIGN
    arm7i_rom = arm9i_rom + STUB_SIZE
    stubs = stub("ARM9i") + stub("ARM7i")

    hdr[0x012] = 0x02
    hdr[0x01C] = DSI_FLAGS
    struct.pack_into("<HH", hdr, 0x090, twl_start, twl_start)
    hdr[0x180:0x1B0] = MBK_SETTINGS
    struct.pack_into("<IIIxxxB", hdr, 0x1B0, REGION_FREE, ACCESS_CONTROL, SCFG_EXT7, APP_FLAGS)
    struct.pack_into("<IIII", hdr, 0x1C0, arm9i_rom, 0, ARM9I_RAM, STUB_SIZE)  # ARM9i: rom, reserved, ram, size
    struct.pack_into("<IIII", hdr, 0x1D0, arm7i_rom, 0, ARM7I_RAM, STUB_SIZE)  # ARM7i: rom, SD list, ram, size
    struct.pack_into("<I", hdr, 0x208, banner_size(rom, struct.unpack_from("<I", hdr, 0x068)[0]))
    struct.pack_into("<I", hdr, 0x210, arm9i_rom + len(stubs))         # total used, including the TWL area
    hdr[0x230:0x234] = hdr[0x00C:0x010][::-1]                          # title ID: game code reversed
    hdr[0x234:0x238] = bytes([0x00, 0x00, 0x03, 0x00])                 # cartridge, DSi
    hdr[0x2F0:0x300] = bytes([0x80] * 0x10)                            # no age restrictions
    struct.pack_into("<H", hdr, 0x15E, crc16(hdr[:HEADER_CRC_END]))
    return hdr, arm9i_rom, stubs


def twilight_accepts(rom: bytes) -> bool:
    """TWiLight Menu++'s checkDsiBinaries(), for a unit code 02h ROM."""
    arm9i_rom = struct.unpack_from("<I", rom, 0x1C0)[0]
    arm7i_rom = struct.unpack_from("<I", rom, 0x1D0)[0]
    reference = rom[0x8000:0x8010]
    for offset in (arm9i_rom, arm7i_rom):
        if not 0x8000 <= offset < 0x20000000:
            return False
        sig = rom[offset:offset + 0x10].ljust(0x10, b"\0")
        if sig in (reference, bytes(0x10), b"\xFF" * 0x10):
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rom")
    parser.add_argument("--check", action="store_true", help="only verify, do not write")
    parser.add_argument("--ds-copy", metavar="OUT", help="write the unpatched (DS header) ROM here before patching")
    args = parser.parse_args()

    with open(args.rom, "rb") as f:
        rom = f.read()

    if args.ds_copy and not args.check:
        if rom[0x012] != 0x00:
            raise SystemExit(f"error: {args.rom} already has a DSi header; rebuild before --ds-copy")
        with open(args.ds_copy, "wb") as f:
            f.write(rom)
        print(f"{args.ds_copy}: DS header copy for TWiLight Menu++ / nds-bootstrap")

    hdr, stubs_at, stubs = expected_image(rom)
    if args.check:
        if rom[:0x1000] != bytes(hdr):
            diff = [i for i in range(0x1000) if rom[i] != hdr[i]]
            print(f"{args.rom}: DSi header differs at {len(diff)} byte(s), first at {diff[0]:#05x}")
            return 1
        if rom[stubs_at:stubs_at + len(stubs)] != stubs:
            print(f"{args.rom}: ARM9i/ARM7i stubs missing at {stubs_at:#x}")
            return 1
        if not twilight_accepts(rom):
            print(f"{args.rom}: TWiLight Menu++ would report the DSi binaries as missing")
            return 1
        print(f"{args.rom}: DSi-enhanced header OK")
        return 0

    with open(args.rom, "r+b") as f:
        f.write(hdr)
        if len(rom) < stubs_at:
            f.seek(len(rom))
            f.write(b"\xFF" * (stubs_at - len(rom)))
        f.seek(stubs_at)
        f.write(stubs)
    print(f"{args.rom}: unit code 02h (NDS+DSi), SCFG_EXT7 {SCFG_EXT7:#010x}, TSC in NDS mode")
    return 0


if __name__ == "__main__":
    sys.exit(main())
