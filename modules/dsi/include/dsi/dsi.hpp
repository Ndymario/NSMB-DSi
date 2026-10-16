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

// DSi mode was set up by a loader running the game from the SD card
// (nds-bootstrap, as TWiLight Menu++ uses it) rather than by the DSi
// launcher. nds-bootstrap already lets the game's own heap grow to about
// 6.3MB, keeps its ROM cache in the rest of upper RAM and owns NWRAM and the
// clock, so the module adds no resources of its own then: see each one below.
bool isLoaderBoot();

// printf-style line (OS_VSNPrintf formats, newline added) to the emulator's
// debug console via the no$gba debug register; melonDS prints it to the
// terminal it was started from. Does nothing on hardware. Lines over 190
// characters are cut.
void log(const char* format, ...) __attribute__((format(printf, 1, 2)));

// --- ExtendedRam ---------------------------------------------------------

// The root heap (where `new` and the game's own scene/actor/file allocations
// come from) was moved into the upper 7.9MB of DSi RAM. False under a loader
// (whose own setup already gives the root heap about 6.3MB).
bool hasExtendedRam();

// ~3.9MB heap at 0x02400000, free for any module to use. nullptr under a loader.
Heap* extendedHeap();

// The ~2.3MB the game used for its root heap in DS mode, now a spare heap.
// nullptr under a loader.
Heap* lowHeap();

// --- FastClock -----------------------------------------------------------

// ARM9 running at 134MHz instead of 67MHz.
bool isFastClock();

// Switches the ARM9 clock. Returns false (and changes nothing) outside DSi
// mode, and under a loader (nds-bootstrap already runs the game at 134MHz). Takes effect immediately; safe to call from game code.
bool setFastClock(bool enable);

// --- NWram ---------------------------------------------------------------

// 768KB heap in the DSi's new shared WRAM (0x03000000), mapped to the ARM9
// only and cached like main RAM. nullptr under a loader, which uses NWRAM. Separate from main RAM, so filling it never
// competes with the game's own allocations.
Heap* nwramHeap();

} // namespace DSi
