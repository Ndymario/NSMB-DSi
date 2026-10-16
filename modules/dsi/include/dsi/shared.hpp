#pragma once

// Memory both CPUs agree on. NSMB is built with the NTR SDK, so its shared
// area is 0x027FF000-0x027FFFFF in every mode: on a DS that is a mirror of
// 0x023FF000, and in DSi mode (16MB, no mirroring) it is simply real RAM that
// both CPUs keep using by the same address.

#include <stdint.h>

namespace dsi {

// NTR SDK mmap_shared.h: "0x027ffc24-0x027ffc2b:(8byte):UNUSED". The SDK's
// larger unused block, 0x027FFA00-0x027FFA7F, is not free in practice:
// nds-bootstrap keeps its card engine's shared state at 0x027FFA0C and its
// unpatched-function table at 0x027FFA40, and NSMB co-op uses 0x027FFA7E.
// Nothing in nds-bootstrap or TWiLight Menu++ touches these 8 bytes.
#ifndef DSI_SHARED_BLOCK
#define DSI_SHARED_BLOCK 0x027FFC24
#endif

constexpr uint8_t SHARED_MAGIC7 = 0xD7;

enum SharedFlags : uint8_t {
	SHARED_DSI_CONSOLE  = 1u << 0, // DSi/3DS hardware, any mode
	SHARED_TWL_MODE     = 1u << 1, // booted in DSi mode with SCFG unlocked and 16MB mapped
	SHARED_BOOT_AREA_OK = 1u << 2, // NTR boot area holds the boot data (copied now or earlier, or a loader put it there)
	SHARED_LOADER       = 1u << 3, // DSi mode set up by a loader such as nds-bootstrap, not the DSi launcher
	SHARED_REPORT       = 1u << 7, // `value` holds the ARM9 BootReport address (Report component)
};

// Written by ARM7 before its OS_Init, read by ARM9 after its own. 8 bytes.
struct SharedBlock {
	uint8_t magic;     // SHARED_MAGIC7 once ARM7 has filled it in
	uint8_t flags;     // SharedFlags
	uint16_t sndexcnt;
	uint32_t value;    // SCFG_EXT7 as ARM7 found it; the Report component then
	                   // replaces it with its report's address, for tools/harness
};
static_assert(sizeof(SharedBlock) == 8);

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
	{ 0xC00, 0x024 }, // boot check info, reset parameter
	{ 0xC2C, 0x054 }, // ROM offset, cartridge info, VBlank count, WM boot buffer
	                  // (0xC24-0xC2B, unused by the SDK, is the shared block)
	{ 0xC80, 0x100 }, // HW_NVRAM_USER_INFO (nickname, language, touch calibration)
	{ 0xE00, 0x160 }, // HW_ROM_HEADER_BUF (the ROM header FS and CARD read)
};
constexpr uintptr_t NTR_SYSTEM_AREA = 0x027FF000;
constexpr uintptr_t TWL_SYSTEM_AREA = 0x02FFF000;

// After a soft reset (OS_ResetSystem, which NSMB uses to switch between the
// main game, Mario vs. Luigi and the minigames) the NTR area is already in
// place and holds state the TWL area does not: the reset parameter at
// 0x027FFC20 that says which mode to boot, and the multiboot info at
// 0x027FFC40. Copying again would wipe them. The ROM header is the last range
// copied and nothing else writes it, so a matching header means the area was
// mirrored on a previous boot.
inline bool bootAreaMirrored()
{
	auto* ntr = reinterpret_cast<const volatile uint32_t*>(NTR_SYSTEM_AREA + 0xE00);
	auto* twl = reinterpret_cast<const volatile uint32_t*>(TWL_SYSTEM_AREA + 0xE00);
	for (uint32_t i = 0; i < 0x160 / 4u; i++)
		if (ntr[i] != twl[i])
			return false;
	return true;
}

// Mirrors the TWL boot area into the NTR one on a cold boot; returns whether
// it copied. Safe to run on both CPUs at once: the source is written only by
// the launcher, so two copies produce the same result.
inline bool mirrorBootArea()
{
	if (bootAreaMirrored())
		return false;

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
	return true;
}

// A loader that runs the game from the SD card in DSi mode (nds-bootstrap with
// "Run in: DSi mode (forced)", which TWiLight Menu++ uses) can only do so with
// a DS header: it treats DSi-enhanced ROMs as DSi-SDK titles, which NSMB is
// not. The DSi launcher, by contrast, never starts a DS-header game in DSi
// mode. So in DSi mode, a valid header of unit code 0 in the NTR boot area
// means a loader set the console up and owns part of its memory. (Started by
// the launcher, that area holds the launcher's leftovers on a cold boot, whose
// CRC does not match, or our own DSi-enhanced header after a soft reset.)
inline uint16_t headerCrc16(const volatile uint8_t* data, uint32_t size)
{
	uint32_t crc = 0xFFFF;
	for (uint32_t i = 0; i < size; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++)
			crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
	}
	return static_cast<uint16_t>(crc);
}

inline bool startedByLoader()
{
	auto* header = reinterpret_cast<const volatile uint8_t*>(NTR_SYSTEM_AREA + 0xE00);
	const uint16_t crc = header[0x15E] | (header[0x15F] << 8);
	return header[0x012] == 0x00 && headerCrc16(header, 0x15E) == crc;
}

} // namespace dsi
