#include <stdint.h>

#include "internal7.hpp"

// In DSi mode the ARM7 runs the DSi BIOS, and its GetPitchTable (SWI 1Bh) is
// broken: it indexes the sine table instead of the pitch table. GBATEK "BIOS
// Sound Functions": "as workaround for obtaining PitchTable values, one can
// set r0=(0..2FFh)-46Ah on DSi". NSMB's sound driver (SND_CalcTimer) gets
// every note's timer from it, so without this the music plays at wrong pitches.
//
// nds-bootstrap has its own fix, which it applies only where it finds this
// veneer unchanged; replacing it here means exactly one fix applies under
// nds-bootstrap too.
//
// It is the only DSi7 BIOS difference NSMB runs into: of the ARM7's SVC
// wrappers, only GetPitchTable and GetVolumeTable have callers, and
// GetVolumeTable is unaffected.

// _Ven_SVC_GetPitchTable (0x037FF5A0): the ARM-to-THUMB veneer SND_CalcTimer
// calls (`ldr ip, =SVC_GetPitchTable+1; bx ip`), and the wrapper's only
// caller. Replaced outright; returns straight to SND_CalcTimer.
ncp_jump(0x037FF5A0)
uint16_t DSi_getPitchTable(int index)
{
	if (dsi::twl7)
		index -= 0x46A;

	register int r0 asm("r0") = index;
	asm volatile("swi #0x1B0000" : "+r"(r0) :: "r1", "r2", "r3", "r12", "lr", "memory");
	return static_cast<uint16_t>(r0);
}
