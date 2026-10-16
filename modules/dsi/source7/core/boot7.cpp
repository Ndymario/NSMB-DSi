#include "dsi/shared.hpp"
#include "dsi/twl_regs.hpp"

// ARM7 side of the core. Runs once, before the ARM7 OS_Init, which is before
// the ARM7 answers the ARM9's PXI handshake. That ordering is what lets the
// ARM9 trust the shared block once its own OS_Init has returned.

using namespace dsi;

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
		flags |= SHARED_TWL_MODE;
		mirrorBootArea();
		flags |= SHARED_BOOT_COPIED;
	}

	blk.sndexcnt = twl::io<uint16_t>(twl::SNDEXCNT);
	blk.scfgRom7 = twl::io<uint16_t>(twl::SCFG_ROM);
	blk.scfgExt7 = ext7;
	blk.mmemChecker7 = *reinterpret_cast<volatile uint16_t*>(0x027FFFFA); // HW_MMEMCHECKER_SUB
	blk.flags = flags;
	blk.magic = SHARED_MAGIC7;
}
