#include <nsmb/core/system/memory.hpp>

#include "dsi/nwram.hpp"
#include "dsi/twl_regs.hpp"
#include "../core/internal.hpp"

// Maps all of the DSi's new shared WRAM (WRAM-A 256K, WRAM-B 256K, WRAM-C 256K)
// to the ARM9 as one 768KB block at 0x03000000, and makes it a heap.
// GBATEK "DSi New Shared WRAM (for ARM7, ARM9, DSP)".
//
// NSMB's NTR code never touches it: the ARM7 keeps the old 32K shared WRAM at
// 0x037F8000 and its own WRAM at 0x03800000, neither of which overlaps, and
// the DSP is unused. NCPatcher's rom.dsi-enhanced writes the same layout into the
// cartridge header so the launcher maps it before the game starts; doing it
// again here keeps the result independent of how faithfully a loader applies
// the header.

using namespace dsi;

namespace {

// Writes only when MBK9 leaves the slot registers writable from ARM9.
bool assignSlots()
{
	const uint32_t protect = twl::io<uint32_t>(twl::MBK9);
	auto writeIfOpen = [protect](uintptr_t reg, uint32_t value, uint32_t mask) {
		if (twl::io<uint32_t>(reg) == value)
			return true;
		if (protect & mask)
			return false;
		twl::io<uint32_t>(reg) = value;
		return twl::io<uint32_t>(reg) == value;
	};
	return writeIfOpen(twl::MBK1, nwram::MBK1, 0x0000000F)
		&& writeIfOpen(twl::MBK2, nwram::MBK2, 0x00000F00)
		&& writeIfOpen(twl::MBK3, nwram::MBK3, 0x0000F000)
		&& writeIfOpen(twl::MBK4, nwram::MBK4, 0x000F0000)
		&& writeIfOpen(twl::MBK5, nwram::MBK5, 0x00F00000);
}

bool probe()
{
	auto* words = reinterpret_cast<volatile uint32_t*>(nwram::BASE);
	const uint32_t last = nwram::SIZE / 4 - 1;
	for (uint32_t block = 0; block < 3; block++) {
		volatile uint32_t* first = words + block * (0x40000 / 4);
		*first = 0x4E57524Du ^ block; // "NWRM"
	}
	words[last] = 0xA5A5A5A5;
	for (uint32_t block = 0; block < 3; block++)
		if (words[block * (0x40000 / 4)] != (0x4E57524Du ^ block))
			return false;
	return words[last] == 0xA5A5A5A5;
}

bool mapped;

} // namespace

extern "C" void DSi_nwramInit()
{
	// SCFG_EXT9 bit25: ARM7 granted the ARM9 access to the new WRAM.
	if (!(twl::io<uint32_t>(twl::SCFG_EXT) & (1u << 25)) || !assignSlots())
		return;

	twl::io<uint32_t>(twl::MBK6) = nwram::MBK6;
	twl::io<uint32_t>(twl::MBK7) = nwram::MBK7;
	twl::io<uint32_t>(twl::MBK8) = nwram::MBK8;

	// The window lives at 0x03000000, outside the 16MB main RAM region.
	state.region1 = REGION1_32MB;
	writeRegion1(state.region1);

	mapped = probe();
}

extern "C" void DSi_nwramAfterRootHeap()
{
	if (mapped)
		state.nwramHeap = Memory::tryCreateExpandedHeap(reinterpret_cast<void*>(nwram::BASE), nwram::SIZE);
}
