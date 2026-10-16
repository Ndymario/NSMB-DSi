#include "dsi/shared.hpp"
#include "dsi/twl_regs.hpp"
#include "internal7.hpp"

// ARM7 side of the core. Runs once, before the ARM7 OS_Init, which is before
// the ARM7 answers the ARM9's PXI handshake. That ordering is what lets the
// ARM9 trust the shared block once its own OS_Init has returned.

using namespace dsi;

namespace dsi { bool twl7; }

// NitroSpMain+0x4 (0x037F846C): `bl OS_Init`, ARM mode, the ARM7's first call.
// Verified against the clean US ARM7 (autoload at 0x037F8000).
ncp_hook(0x037F846C)
void DSi_bootArm7()
{
	volatile SharedBlock& blk = shared();

	uint32_t flags = 0;
	if (twl::sndexcntPresent())
		flags |= SHARED_DSI_CONSOLE;

	const uint32_t ext7 = twl::io<uint32_t>(twl::SCFG_EXT);
	const bool twlMode = (ext7 & twl::SCFG_EXT_ACCESS)
		&& ((ext7 & twl::SCFG_EXT_RAM_MASK) >> twl::SCFG_EXT_RAM_SHIFT) >= twl::SCFG_EXT_RAM_16MB;

	if (twlMode) {
		twl7 = true;
		// Keep NITRO behaviour for the hardware the ARM7 code already uses.
		twl::io<uint32_t>(twl::SCFG_EXT) = ext7 & ~twl::SCFG_EXT7_NITRO_CLEAR;
		flags |= SHARED_TWL_MODE;
		mirrorBootArea(); // no-op after a soft reset, or if the ARM9 got there first
		flags |= SHARED_BOOT_AREA_OK;
	}

	blk.sndexcnt = twl::io<uint16_t>(twl::SNDEXCNT);
	blk.scfgRom7 = twl::io<uint16_t>(twl::SCFG_ROM);
	blk.scfgExt7 = ext7;
	blk.mmemChecker7 = *reinterpret_cast<volatile uint16_t*>(0x027FFFFA); // HW_MMEMCHECKER_SUB
	blk.flags = flags;
	blk.magic = SHARED_MAGIC7;
}
