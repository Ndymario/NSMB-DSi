#include <nsmb_nitro.hpp>

#include "dsi/dsi.hpp"
#include "dsi/twl_regs.hpp"
#include "internal.hpp"

// ARM9 clock switching (SCFG_CLK bit0, 67MHz <-> 134MHz).
//
// GBATEK: "Change ARM9 clock only from code within ITCM (and wait at least 8
// cycles before accessing any non-ITCM memory)". So the write happens in a
// small stub copied into the ITCM arena, with interrupts off. NSMB leaves the
// ITCM arena (0x01FFFFA0-0x02000000) unused; nothing in main or the overlays
// allocates from it.

extern "C" const uint32_t DSi_clockStub[];
extern "C" const uint32_t DSi_clockStubEnd[];

// void stub(u16 scfgClk): position independent, no literal pool.
asm(R"(
	.section .rodata.DSi_clockStub, "a"
	.balign 4
	.global DSi_clockStub
DSi_clockStub:
	mov     r1, #0x04000000
	add     r1, r1, #0x4000
	strh    r0, [r1, #4]        @ SCFG_CLK
	mov     r0, r0              @ 8 cycles before leaving ITCM
	mov     r0, r0
	mov     r0, r0
	mov     r0, r0
	mov     r0, r0
	mov     r0, r0
	mov     r0, r0
	mov     r0, r0
	bx      lr
	.global DSi_clockStubEnd
DSi_clockStubEnd:
	.text
)");

namespace {

using ClockStub = void (*)(uint16_t);
ClockStub itcmStub;

ClockStub installStub()
{
	const uint32_t size = reinterpret_cast<uintptr_t>(DSi_clockStubEnd) - reinterpret_cast<uintptr_t>(DSi_clockStub);
	auto* dst = static_cast<uint32_t*>(OS_AllocFromArenaLo(OS_ARENA_ITCM, size, 4));
	if (!dst)
		return nullptr;
	for (uint32_t i = 0; i < size / 4; i++)
		dst[i] = DSi_clockStub[i];
	DC_WaitWriteBufferEmpty();
	return reinterpret_cast<ClockStub>(dst);
}

} // namespace

bool DSi::setFastClock(bool enable)
{
	using namespace dsi;

	if (!state.twl || !twl::scfgUnlocked())
		return false;
	if (!itcmStub && !(itcmStub = installStub()))
		return false;

	uint16_t clk = twl::io<uint16_t>(twl::SCFG_CLK);
	clk = enable ? (clk | 1) : (clk & ~1);

	OSIntrMode irq = OS_DisableInterrupts();
	itcmStub(clk);
	OS_RestoreInterrupts(irq);

	state.fastClock = (twl::io<uint16_t>(twl::SCFG_CLK) & 1) != 0;
	return state.fastClock == enable;
}
