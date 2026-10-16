#include <nsmb/core/system/memory.hpp>

#include "../core/internal.hpp"

// Puts the DSi's extra 12MB of main RAM to work.
//
// In DSi mode main RAM is 16MB and no longer mirrors every 4MB, so the areas
// the NTR SDK never touches become real, free memory:
//
//   0x0219xxxx-0x023E0000  vanilla root heap (main arena)     -> lowHeap()
//   0x023E0000-0x02400000  MPU region 2 guard, boot leftovers  (left alone)
//   0x02400000-0x027E0000  free                               -> extendedHeap()
//   0x027E0000-0x02800000  ARM7 code/data, DTCM, shared area   (left alone)
//   0x02800000-0x02FE0000  free                               -> root heap
//   0x02FE0000-0x02FFFFFF  DSi launcher data and system area (left alone)
//
// The root heap is what nearly every allocation in the game comes from, so
// moving it into the largest block gives vanilla code ~3.5x the memory with no
// other patch. Only Memory::initRootHeap reads the main arena (checked against
// main and all 131 overlays), so changing the arena is all it takes.

#ifndef DSI_ROOT_HEAP_START
#define DSI_ROOT_HEAP_START 0x02800000
#endif
// Not 0x02FFC000: the 112KB below the DSi system area hold launcher data DSi
// software reads (melonDS even fakes the word at 0x02FE71B0, its region-lock
// check, which corrupted whatever the game had stored there).
#ifndef DSI_ROOT_HEAP_END
#define DSI_ROOT_HEAP_END 0x02FE0000
#endif
#ifndef DSI_EXT_HEAP_START
#define DSI_EXT_HEAP_START 0x02400000
#endif
#ifndef DSI_EXT_HEAP_END
#define DSI_EXT_HEAP_END 0x027E0000
#endif

namespace {

void* lowStart;
void* lowEnd;

Heap* makeHeap(void* start, void* end)
{
	uintptr_t s = (reinterpret_cast<uintptr_t>(start) + 31) & ~31u;
	uintptr_t e = reinterpret_cast<uintptr_t>(end) & ~31u;
	if (e <= s + 0x1000)
		return nullptr;
	return Memory::tryCreateExpandedHeap(reinterpret_cast<void*>(s), e - s);
}

} // namespace

extern "C" void DSi_extramBeforeRootHeap()
{
	lowStart = OS_GetArenaLo(OS_ARENA_MAIN);
	lowEnd = OS_GetArenaHi(OS_ARENA_MAIN);

	OS_SetArenaLo(OS_ARENA_MAIN, reinterpret_cast<void*>(DSI_ROOT_HEAP_START));
	OS_SetArenaHi(OS_ARENA_MAIN, reinterpret_cast<void*>(DSI_ROOT_HEAP_END));
}

extern "C" void DSi_extramAfterRootHeap()
{
	using dsi::state;

	state.extendedRam = Memory::rootHeapPtr != nullptr;
	if (!state.extendedRam)
		return;

	state.extendedHeap = makeHeap(reinterpret_cast<void*>(DSI_EXT_HEAP_START), reinterpret_cast<void*>(DSI_EXT_HEAP_END));
	state.lowHeap = makeHeap(lowStart, lowEnd);
}
