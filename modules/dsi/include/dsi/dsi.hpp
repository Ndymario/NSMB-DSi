#pragma once

// Public API of the DSi module.
//
// Other modules can test for it with `#ifdef MODULE_DSI`, and must treat every
// resource as optional: on a DS, on a DSi running the game in DS mode, or with
// the providing component switched off, the queries return false / nullptr.
//
// Everything is settled during boot (before the first heap is created), so the
// answers never change while the game runs, except for the CPU clock.

class Heap;

namespace DSi {

// --- Core ----------------------------------------------------------------

// Running on DSi or 3DS hardware, in either mode. Valid once OS_Init has run.
bool isConsole();

// Booted in DSi mode with SCFG still accessible and 16MB of main RAM mapped.
// Every resource below requires this.
bool isTwlMode();

// printf-style line (OS_VSNPrintf formats, newline added) to the emulator's
// debug console via the no$gba debug register; melonDS prints it to the
// terminal it was started from. Does nothing on hardware. Lines over 190
// characters are cut.
void log(const char* format, ...) __attribute__((format(printf, 1, 2)));

// --- ExtendedRam ---------------------------------------------------------

// The root heap (where `new` and the game's own scene/actor/file allocations
// come from) was moved into the upper 8MB of DSi RAM.
bool hasExtendedRam();

// ~3.9MB heap at 0x02400000, free for any module to use.
Heap* extendedHeap();

// The ~2.3MB the game used for its root heap in DS mode, now a spare heap.
Heap* lowHeap();

// --- FastClock -----------------------------------------------------------

// ARM9 running at 134MHz instead of 67MHz.
bool isFastClock();

// Switches the ARM9 clock. Returns false (and changes nothing) outside DSi
// mode. Takes effect immediately; safe to call from game code.
bool setFastClock(bool enable);

// --- NWram ---------------------------------------------------------------

// 768KB heap in the DSi's new shared WRAM (0x03000000), mapped to the ARM9
// only and cached like main RAM. Separate from main RAM, so filling it never
// competes with the game's own allocations.
Heap* nwramHeap();

} // namespace DSi
