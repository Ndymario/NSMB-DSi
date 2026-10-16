#pragma once

// DSi (TWL) hardware this module touches, shared by the ARM9 and ARM7 code.
// Addresses and bit meanings are from GBATEK: "DSi Control Registers (SCFG)",
// "DSi New Shared WRAM (for ARM7, ARM9, DSP)" and "DSi Cartridge Header".
// Nothing here may be written without first checking dsi::twl::scfgUnlocked().

#include <stdint.h>

namespace dsi::twl {

template<typename T>
inline volatile T& io(uintptr_t address) { return *reinterpret_cast<volatile T*>(address); }

// ARM9: SCFG_A9ROM (R).  ARM7: SCFG_ROM (R/W, set-once).
constexpr uintptr_t SCFG_ROM = 0x04004000;
// ARM9: bit0 = ARM9 clock (0 = 67MHz, 1 = 134MHz).  Change only from ITCM.
constexpr uintptr_t SCFG_CLK = 0x04004004;
// ARM9: SCFG_EXT9, ARM7: SCFG_EXT7.  bit31 = SCFG/MBK access, bit14-15 = RAM limit.
constexpr uintptr_t SCFG_EXT = 0x04004008;

constexpr uintptr_t MBK1 = 0x04004040; // WRAM-A slots, bank 0-3 (global)
constexpr uintptr_t MBK2 = 0x04004044; // WRAM-B slots, bank 0-3 (global)
constexpr uintptr_t MBK3 = 0x04004048; // WRAM-B slots, bank 4-7 (global)
constexpr uintptr_t MBK4 = 0x0400404C; // WRAM-C slots, bank 0-3 (global)
constexpr uintptr_t MBK5 = 0x04004050; // WRAM-C slots, bank 4-7 (global)
constexpr uintptr_t MBK6 = 0x04004054; // WRAM-A address range (local to each CPU)
constexpr uintptr_t MBK7 = 0x04004058; // WRAM-B address range (local)
constexpr uintptr_t MBK8 = 0x0400405C; // WRAM-C address range (local)
constexpr uintptr_t MBK9 = 0x04004060; // WRAM-A/B/C slot write protect

// Readable even in DS mode on a DSi; reads as 0 on a DS (GBATEK "DSi I/O Map").
constexpr uintptr_t SNDEXCNT = 0x04004700;

constexpr uint32_t SCFG_EXT_ACCESS        = 1u << 31;
constexpr uint32_t SCFG_EXT_RAM_SHIFT     = 14;
constexpr uint32_t SCFG_EXT_RAM_MASK      = 3u << SCFG_EXT_RAM_SHIFT;
constexpr uint32_t SCFG_EXT_RAM_16MB      = 2;

constexpr uint16_t SCFG_A9ROM_TWL         = 0x0001; // DSi BIOS, lower 32K: "DSi console in DSi mode"

// SCFG_EXT reads as zero once access is revoked (and always on a DS), so a set
// access bit means we are in DSi mode with the registers still writable.
inline bool scfgUnlocked() { return (io<uint32_t>(SCFG_EXT) & SCFG_EXT_ACCESS) != 0; }

inline uint32_t ramLimit() { return (io<uint32_t>(SCFG_EXT) & SCFG_EXT_RAM_MASK) >> SCFG_EXT_RAM_SHIFT; }

// The DSi audio mixer register is wired on DSi hardware in both modes and
// absent on a DS. This is the same probe NSMB co-op's DSiModeWarn uses.
inline bool sndexcntPresent() { return (io<uint16_t>(SNDEXCNT) & 0xA000) == 0x8000; }

} // namespace dsi::twl
