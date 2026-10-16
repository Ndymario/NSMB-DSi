#include "dsi/dsi.hpp"

// Runs the ARM9 at 134MHz in DSi mode. The game's frame pacing comes from
// VBlank, so it plays at the same speed; it just stops dropping frames when
// a level gets busy. Without this component the core sets the NDS clock, so a
// launcher that already switched to 134MHz does not change the game silently.

extern "C" void DSi_clockInit()
{
	DSi::setFastClock(true);
}
