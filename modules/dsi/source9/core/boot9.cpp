#include "dsi/dsi.hpp"
#include "dsi/shared.hpp"
#include "dsi/twl_regs.hpp"
#include "internal.hpp"

// ARM9 side of the core: decides once, as early as possible, whether the game
// is running in DSi mode, makes the NTR SDK's view of memory hold up there,
// and runs the other components' boot stages in a fixed order.

namespace dsi { State state; }

using namespace dsi;

bool DSi::isConsole()      { return state.console; }
bool DSi::isTwlMode()      { return state.twl; }
bool DSi::hasExtendedRam() { return state.extendedRam; }
Heap* DSi::extendedHeap()  { return state.extendedHeap; }
Heap* DSi::lowHeap()       { return state.lowHeap; }
Heap* DSi::nwramHeap()     { return state.nwramHeap; }
bool DSi::isFastClock()    { return state.fastClock; }

// App::branchToStartup (0x020056A8): `stmfd sp!, {lr}`, ARM mode. crt0 reaches it
// through the NitroStartUp veneer after autoload, before static initializers
// and NitroMain; it dispatches to App::commonStartup, which runs OS_Init. So
// this is the first game code that runs, and nothing has read the boot area yet.
ncp_hook(0x020056A8)
void DSi_bootArm9()
{
	state.scfgA9Rom = twl::io<uint16_t>(twl::SCFG_ROM);
	state.scfgExt9 = twl::io<uint32_t>(twl::SCFG_EXT);
	state.region1Boot = readRegion1();
	state.twl = state.scfgA9Rom == twl::SCFG_A9ROM_TWL
		&& (state.scfgExt9 & twl::SCFG_EXT_ACCESS)
		&& twl::ramLimit() >= twl::SCFG_EXT_RAM_16MB;

	if (!state.twl)
		return;

	// crt0 maps 0x02000000-0x027FFFFF only; the launcher's data sits at 0x02FFFxxx.
	state.region1 = REGION1_16MB;
	writeRegion1(state.region1);
	mirrorBootArea();
}

// App::commonStartup+0x24 (0x02004E44): `bl Memory::initRootHeap`, right after
// OS_Init and before the first heap exists.
ncp_hook(0x02004E44)
void DSi_afterOsInit()
{
	// OS_Init has completed the PXI handshake, so ARM7 has filled in the block.
	const volatile SharedBlock& blk = shared();
	state.console = state.twl || (blk.magic == SHARED_MAGIC7 && (blk.flags & SHARED_DSI_CONSOLE));

	if (!state.twl)
		return;

	if (DSi_nwramInit)
		DSi_nwramInit(); // may widen region 1, so it goes first

	// OS_InitArenaEx shrinks region 1 to 4MB when it believes the console is retail.
	writeRegion1(state.region1);

	if (DSi_clockInit)
		DSi_clockInit();
	else
		DSi::setFastClock(false);

	if (DSi_extramBeforeRootHeap)
		DSi_extramBeforeRootHeap();
}

// App::commonStartup+0x28 (0x02004E48): `bl 0x02012764` (an empty function),
// the first instruction after Memory::initRootHeap returns.
ncp_hook(0x02004E48)
void DSi_afterRootHeap()
{
	if (state.twl) {
		if (DSi_extramAfterRootHeap)
			DSi_extramAfterRootHeap();
		if (DSi_nwramAfterRootHeap)
			DSi_nwramAfterRootHeap();
	}

	if (DSi_collectReport)
		DSi_collectReport();
}
