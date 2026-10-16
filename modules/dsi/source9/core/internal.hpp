#pragma once

#include <stdint.h>

class Heap;

namespace dsi {

struct State {
	bool console;
	bool twl;
	bool extendedRam;
	bool fastClock;
	uint16_t scfgA9Rom;
	uint32_t scfgExt9;    // as found at boot, before anything changed it
	uint32_t region1Boot; // MPU region 1 as crt0 left it
	uint32_t region1;     // what the module keeps region 1 at in DSi mode
	Heap* extendedHeap;
	Heap* lowHeap;
	Heap* nwramHeap;
};

extern State state;

// CP15 protection region 1 (main RAM), GBATEK "ARM CP15 Protection Unit".
// 0x0200002F = base 0x02000000, 16MB, enabled: all of DSi main RAM.
// 0x02000031 = base 0x02000000, 32MB, enabled: main RAM plus the WRAM space at
// 0x03000000, which is what the DSi launcher itself uses for TWL titles.
constexpr uint32_t REGION1_16MB = 0x0200002F;
constexpr uint32_t REGION1_32MB = 0x02000031;

inline uint32_t readRegion1()
{
	uint32_t value;
	asm volatile("mrc p15, 0, %0, c6, c1, 0" : "=r"(value));
	return value;
}

inline void writeRegion1(uint32_t value)
{
	asm volatile("mcr p15, 0, %0, c6, c1, 0" :: "r"(value) : "memory");
}

} // namespace dsi

// Boot stages the core drives. Each component that needs one defines it; a
// component that is switched off leaves the weak reference null.
extern "C" {
	void DSi_extramBeforeRootHeap() __attribute__((weak)); // ExtendedRam
	void DSi_extramAfterRootHeap() __attribute__((weak));  // ExtendedRam
	void DSi_clockInit() __attribute__((weak));            // FastClock
	void DSi_nwramInit() __attribute__((weak));            // NWram
	void DSi_nwramAfterRootHeap() __attribute__((weak));   // NWram
	void DSi_collectReport() __attribute__((weak));        // Report
}
