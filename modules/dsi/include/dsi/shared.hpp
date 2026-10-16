#pragma once

// Memory both CPUs agree on. NSMB is built with the NTR SDK, so its shared
// area is 0x027FF000-0x027FFFFF in every mode: on a DS that is a mirror of
// 0x023FF000, and in DSi mode (16MB, no mirroring) it is simply real RAM that
// both CPUs keep using by the same address.

#include <stdint.h>

namespace dsi {

// NTR SDK mmap_shared.h: "0x027ffa00-0x027ffa7f: (128byte): UNUSED".
// NSMB co-op's DSiModeWarn uses 0x027FFA7E, so this block stays well clear.
#ifndef DSI_SHARED_BLOCK
#define DSI_SHARED_BLOCK 0x027FFA40
#endif

constexpr uint32_t SHARED_MAGIC7 = 0x37695344; // "DSi7"

enum SharedFlags : uint32_t {
	SHARED_DSI_CONSOLE  = 1u << 0, // DSi/3DS hardware, any mode
	SHARED_TWL_MODE     = 1u << 1, // booted in DSi mode with SCFG unlocked and 16MB mapped
	SHARED_BOOT_COPIED  = 1u << 2, // TWL boot area mirrored into the NTR shared area
};

// Written by ARM7 before its OS_Init, read by ARM9. 32 bytes.
struct SharedBlock {
	uint32_t magic;        // SHARED_MAGIC7 once ARM7 has filled it in
	uint16_t sndexcnt;
	uint16_t scfgRom7;
	uint32_t scfgExt7;
	uint32_t flags;        // SharedFlags
	uint16_t mmemChecker7; // what NTR crt0 decided the RAM size was (1 = 4MB, 2 = 8MB+)
	uint16_t reserved0;
	uint32_t reserved1;
	uint32_t reserved2;
	uint32_t arm9Report;   // address of the ARM9 BootReport, for tools/harness
};
static_assert(sizeof(SharedBlock) == 0x20);

inline volatile SharedBlock& shared() { return *reinterpret_cast<volatile SharedBlock*>(DSI_SHARED_BLOCK); }

// The DSi launcher writes the boot-time system area (cart header, boot
// indicator, user settings, ...) at 0x02FFFxxx for DSi-mode titles, but the
// NTR SDK reads it at 0x027FFxxx. These are the ranges that have to be
// mirrored, as offsets into the 4KB area. 0xFD80-0xFE00 is deliberately
// skipped: it holds the arena table and the exception vector, which OS_Init
// fills in on its own.
struct BootRange { uint16_t offset, size; };
inline constexpr BootRange BOOT_RANGES[] = {
	{ 0xA80, 0x160 }, // HW_CARD_ROM_HEADER
	{ 0xC00, 0x080 }, // boot check info, reset parameter, ROM offset, WM boot buffer
	{ 0xC80, 0x100 }, // HW_NVRAM_USER_INFO (nickname, language, touch calibration)
	{ 0xE00, 0x160 }, // HW_ROM_HEADER_BUF (the ROM header FS and CARD read)
};
constexpr uintptr_t NTR_SYSTEM_AREA = 0x027FF000;
constexpr uintptr_t TWL_SYSTEM_AREA = 0x02FFF000;

// Mirrors the TWL boot area into the NTR one. Safe to run on both CPUs: the
// source is written once by the launcher, so both copies produce the same result.
inline void mirrorBootArea()
{
	for (const BootRange& r : BOOT_RANGES) {
		auto* src = reinterpret_cast<const volatile uint32_t*>(TWL_SYSTEM_AREA + r.offset);
		auto* dst = reinterpret_cast<volatile uint32_t*>(NTR_SYSTEM_AREA + r.offset);
		for (uint32_t i = 0; i < r.size / 4u; i++)
			dst[i] = src[i];
	}
	// The NTR firmware also leaves the card ID at 0x027FF800/804 (HW_RED_RESERVED).
	auto* cartId = reinterpret_cast<volatile uint32_t*>(TWL_SYSTEM_AREA + 0xC00);
	auto* red = reinterpret_cast<volatile uint32_t*>(NTR_SYSTEM_AREA + 0x800);
	red[0] = *cartId;
	red[1] = *cartId;
}

} // namespace dsi
