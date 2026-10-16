#pragma once

// Layout of the boot report the Report component fills in (source9/report).
// Plain data with natural alignment so tools/harness can read it straight out
// of emulated RAM; bump REPORT_VERSION whenever a field changes.

#include <stdint.h>

#include "dsi/shared.hpp"

namespace dsi {

constexpr uint32_t REPORT_MAGIC = 0x39695344; // "DSi9"
constexpr uint32_t REPORT_VERSION = 3;
constexpr int REPORT_WORK_WINDOW = 64;
constexpr int REPORT_ARENAS = 7; // main, main-subpriv, main-ex, ITCM, DTCM, shared, WRAM-main

struct HeapInfo {
	uint32_t heap;           // Heap* (0 if absent)
	uint32_t start;          // first allocatable byte
	uint32_t size;
	uint32_t freeNow;
	uint32_t freeMin;        // lowest free size seen since boot
};

struct BootReport {
	uint32_t magic;
	uint32_t version;
	uint32_t flags;          // bit0 DSi console, bit1 DSi mode, bit2 extended RAM, bit3 fast clock
	uint16_t scfgA9Rom;
	uint16_t scfgClk;
	uint32_t scfgExt9Boot;   // before the module changed anything
	uint32_t scfgExt9Now;
	uint32_t mbk[9];
	uint32_t region1Boot;    // MPU region 1 as crt0 left it
	uint32_t mpu[8];         // after boot
	uint32_t consoleType;    // OS_GetConsoleType()
	uint32_t arenaLo[REPORT_ARENAS];
	uint32_t arenaHi[REPORT_ARENAS];
	uint16_t mmemChecker9;   // HW_MMEMCHECKER_MAIN
	uint16_t mmemChecker7;   // HW_MMEMCHECKER_SUB
	uint32_t ntrHeader[4];   // first 16 bytes of 0x027FFE00 (game code at +0x0C)
	uint32_t twlHeader[4];   // first 16 bytes of 0x02FFFE00
	uint32_t ntrUser[4];     // first 16 bytes of 0x027FFC80 (user settings)
	SharedBlock arm7;        // the shared block as ARM7 left it

	// Updated every frame from here on.
	uint32_t frames;
	uint32_t workTicks[REPORT_WORK_WINDOW]; // OS ticks (523.656kHz) of game work per frame, ring
	                                        // buffer indexed by frames; excludes the VBlank wait
	HeapInfo root;
	HeapInfo extended;
	HeapInfo low;
	HeapInfo nwram;
};

} // namespace dsi
