#include <nsmb/core/system/memory.hpp>

#include "dsi/report.hpp"
#include "dsi/shared.hpp"
#include "dsi/twl_regs.hpp"
#include "../core/internal.hpp"

// A snapshot of what the module set up at boot, plus per-frame heap and CPU
// load figures, so tools/harness (or a debugger) can check a run without
// anything on screen. Its address is published in the shared block's
// arm9Report field. Development aid: costs a few hundred bytes and two hooks
// in the main loop, so leave it off in releases.

namespace {

dsi::BootReport report;
OSTick frameStart;

template<int N>
uint32_t readRegion()
{
	uint32_t value;
	asm volatile("mrc p15, 0, %0, c6, c%c1, 0" : "=r"(value) : "i"(N));
	return value;
}

void copyWords(void* dst, uintptr_t src, int count)
{
	for (int i = 0; i < count; i++)
		static_cast<uint32_t*>(dst)[i] = reinterpret_cast<const volatile uint32_t*>(src)[i];
}

void describe(dsi::HeapInfo& info, Heap* heap)
{
	info.heap = reinterpret_cast<uintptr_t>(heap);
	if (!heap)
		return;
	info.start = reinterpret_cast<uintptr_t>(heap->heapStart);
	info.size = heap->heapSize;
	info.freeNow = info.freeMin = heap->vMemoryLeft();
}

void sample(dsi::HeapInfo& info)
{
	if (!info.heap)
		return;
	info.freeNow = reinterpret_cast<Heap*>(info.heap)->vMemoryLeft();
	if (info.freeNow < info.freeMin)
		info.freeMin = info.freeNow;
}

// The harness reads RAM directly; make sure the report is not sitting in the D-cache.
void flush()
{
	DC_FlushRange(&report, sizeof(report));
	DC_WaitWriteBufferEmpty();
}

} // namespace

extern "C" void DSi_collectReport()
{
	using namespace dsi;

	BootReport& r = report;
	r.magic = REPORT_MAGIC;
	r.version = REPORT_VERSION;
	r.flags = (state.console ? 1u : 0u) | (state.twl ? 2u : 0u)
		| (state.extendedRam ? 4u : 0u) | (state.fastClock ? 8u : 0u);
	r.scfgA9Rom = state.scfgA9Rom;
	r.scfgExt9Boot = state.scfgExt9;
	r.scfgExt9Now = twl::io<uint32_t>(twl::SCFG_EXT);
	r.region1Boot = state.region1Boot;

	if (state.twl) {
		r.scfgClk = twl::io<uint16_t>(twl::SCFG_CLK);
		for (int i = 0; i < 9; i++)
			r.mbk[i] = twl::io<uint32_t>(twl::MBK1 + i * 4);
	}

	r.mpu[0] = readRegion<0>();
	r.mpu[1] = readRegion<1>();
	r.mpu[2] = readRegion<2>();
	r.mpu[3] = readRegion<3>();
	r.mpu[4] = readRegion<4>();
	r.mpu[5] = readRegion<5>();
	r.mpu[6] = readRegion<6>();
	r.mpu[7] = readRegion<7>();

	r.consoleType = OS_GetConsoleType();
	for (int i = 0; i < REPORT_ARENAS; i++) {
		r.arenaLo[i] = reinterpret_cast<uintptr_t>(OS_GetArenaLo(static_cast<OSArenaId>(i)));
		r.arenaHi[i] = reinterpret_cast<uintptr_t>(OS_GetArenaHi(static_cast<OSArenaId>(i)));
	}

	r.mmemChecker9 = *reinterpret_cast<volatile uint16_t*>(0x027FFFF8);
	r.mmemChecker7 = *reinterpret_cast<volatile uint16_t*>(0x027FFFFA);
	copyWords(r.ntrHeader, 0x027FFE00, 4);
	copyWords(r.ntrUser, 0x027FFC80, 4);
	if (state.twl)
		copyWords(r.twlHeader, 0x02FFFE00, 4);

	copyWords(&r.arm7, DSI_SHARED_BLOCK, sizeof(SharedBlock) / 4);

	describe(r.root, Memory::rootHeapPtr);
	describe(r.extended, state.extendedHeap);
	describe(r.low, state.lowHeap);
	describe(r.nwram, state.nwramHeap);

	shared().arm9Report = reinterpret_cast<uintptr_t>(&r);
	flush();
}

// App::gameLoop+0x6C (0x02004F14): `bl Game::waitVBlankIntr`, the end of a frame's work.
ncp_hook(0x02004F14)
void DSi_reportFrameEnd()
{
	dsi::BootReport& r = report;
	if (frameStart != 0) {
		r.workTicks[r.frames % dsi::REPORT_WORK_WINDOW] = static_cast<uint32_t>(OS_GetTick() - frameStart);
		r.frames++;
	}

	sample(r.root);
	sample(r.extended);
	sample(r.low);
	sample(r.nwram);
	flush();
}

// App::gameLoop+0x70 (0x02004F18): `bl Net::update`, the first call after the VBlank wait.
ncp_hook(0x02004F18)
void DSi_reportFrameStart()
{
	frameStart = OS_GetTick();
}
