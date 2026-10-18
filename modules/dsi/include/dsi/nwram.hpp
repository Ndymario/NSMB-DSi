#pragma once

// The NWRAM layout the NWram component sets up, as register values.
// NCPatcher's rom.dsi-enhanced writes the same numbers into the cartridge header
// (keep the two in sync). GBATEK "DSi New Shared WRAM (for ARM7, ARM9, DSP)".

#include <stdint.h>

namespace dsi::nwram {

constexpr uintptr_t BASE = 0x03000000;
constexpr uint32_t SIZE = 0xC0000; // A, B and C back to back: 3 x 256KB

// MBK1-5, one byte per slot: bit7 enable, bits 2-4 slot offset, bits 0-1 master (0 = ARM9).
constexpr uint32_t MBK1 = 0x8C888480; // WRAM-A banks 0-3 -> slots 0-3
constexpr uint32_t MBK2 = 0x8C888480; // WRAM-B banks 0-3 -> slots 0-3
constexpr uint32_t MBK3 = 0x9C989490; // WRAM-B banks 4-7 -> slots 4-7
constexpr uint32_t MBK4 = 0x8C888480; // WRAM-C banks 0-3 -> slots 0-3
constexpr uint32_t MBK5 = 0x9C989490; // WRAM-C banks 4-7 -> slots 4-7

// MBK6: start (bits 4-11) and end (bits 20-28) in 64KB units from 0x03000000,
// image size 3 (all four slots). 0x03000000-0x0303FFFF.
constexpr uint32_t MBK6 = (0u << 4) | (3u << 12) | (4u << 20);
// MBK7/8: start (bits 3-11) and end (bits 19-28) in 32KB units, image size 3 (all eight slots).
constexpr uint32_t MBK7 = (8u << 3) | (3u << 12) | (16u << 19);  // 0x03040000-0x0307FFFF
constexpr uint32_t MBK8 = (16u << 3) | (3u << 12) | (24u << 19); // 0x03080000-0x030BFFFF

} // namespace dsi::nwram
