#include <stdarg.h>

#include <nsmb/core/system/memory.hpp>

#include "dsi/dsi.hpp"
#include "internal.hpp"

// Debug output through the no$gba "String Out (raw)" register, which melonDS
// supports out of the box (it prints to the terminal melonDS was started
// from). GBATEK "No$gba Emulator Debug Registers". On hardware the address is
// unmapped, so the write does nothing.

namespace {

constexpr uintptr_t NOCASH_STRING_OUT = 0x04FFFA10;

// The emulator reads the string over the bus, so it must be in main RAM (not
// DTCM, where the stack lives) and written back from the D-cache.
char buffer[192];

uint32_t kilobytes(Heap* heap)
{
	return heap ? heap->heapSize / 1024 : 0;
}

} // namespace

void DSi::log(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	int length = OS_VSNPrintf(buffer, sizeof(buffer) - 1, format, args);
	va_end(args);

	if (length < 0)
		return;
	if (length > static_cast<int>(sizeof(buffer)) - 2)
		length = sizeof(buffer) - 2;
	buffer[length] = '\n';
	buffer[length + 1] = '\0';

	DC_FlushRange(buffer, sizeof(buffer));
	DC_WaitWriteBufferEmpty();
	*reinterpret_cast<volatile uint32_t*>(NOCASH_STRING_OUT) = reinterpret_cast<uintptr_t>(buffer);
}

void dsi::logBootSummary()
{
	if (!state.twl) {
		DSi::log("[DSi] DS mode%s: module inactive, game unchanged",
			state.console ? " on DSi hardware" : "");
		return;
	}

	DSi::log("[DSi] DSi mode%s: ARM9 %s MHz, root heap %p (%u KB)",
		state.loader ? " (set up by a loader)" : "",
		state.fastClock ? "134" : "67", Memory::rootHeapPtr, kilobytes(Memory::rootHeapPtr));
	DSi::log("[DSi] extra heaps: extended %u KB, low %u KB, NWRAM %u KB",
		kilobytes(state.extendedHeap), kilobytes(state.lowHeap), kilobytes(state.nwramHeap));
}
