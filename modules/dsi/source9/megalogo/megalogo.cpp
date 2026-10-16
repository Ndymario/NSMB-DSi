#include <nsmb/core/system/memory.hpp>
#include <nsmb/game/sound.hpp>

#include "dsi/dsi.hpp"

// A small tell that the game is running in DSi mode: the Nintendo logo's
// coin "ding" (SE_SYS_NINTENDO_LOGO) becomes the sound of a Mega Mushroom
// coming out of a block (SE_OBJ_HUGE_KINO_APP). DS mode is unchanged.
//
// The Mega Mushroom sound lives in a sound group the game has not loaded yet
// at that point, and the game's own sound heap has no room for it. So, in DSi
// mode only, the group is loaded into a private NNS sound heap carved out of
// DSi::extendedHeap() (or, under nds-bootstrap, which has no extended heap but
// lets the root heap grow to ~6.3MB, out of the root heap), and the heap is
// destroyed again when the logo scene ends. Destroying it runs NNS's dispose callbacks, which clear the sound
// archive's file pointers before the title screen loads its own sounds. If
// anything fails, the original ding plays.

namespace {

constexpr u32 SOUND_HEAP_SIZE = 0x100000; // 1MB, for a few seconds

Heap* soundHeapParent;
void* soundHeapMemory;
NNSSndHeapHandle soundHeap;

void releaseSoundHeap()
{
	if (soundHeap)
		NNS_SndHeapDestroy(soundHeap);
	if (soundHeapMemory)
		soundHeapParent->deallocate(soundHeapMemory);
	soundHeap = nullptr;
	soundHeapMemory = nullptr;
}

bool loadMegaSound()
{
	Heap* heap = DSi::extendedHeap();
	if (!heap && DSi::isLoaderBoot())
		heap = Memory::rootHeapPtr;
	if (!heap || !(soundHeapMemory = heap->allocate(SOUND_HEAP_SIZE, 32)))
		return false;
	soundHeapParent = heap;

	soundHeap = NNS_SndHeapCreate(soundHeapMemory, SOUND_HEAP_SIZE);
	if (soundHeap && NNS_SndArcLoadGroup(SndGroupID::GROUP_VS_SE, soundHeap))
		return true;

	releaseSoundHeap();
	return false;
}

} // namespace

// BootScene, overlay 1, 0x020CCDDC: `bl SND::playSFX(s32, const Vec3*)` with
// r0 = SE_SYS_NINTENDO_LOGO (232), right after SND::loadMenuSounds(-1).
// The logo's only sound; ARM mode.
ncp_call(0x020CCDDC, 1)
bool DSi_logoSound(s32 sfxID, const Vec3* pos)
{
	if (DSi::isTwlMode() && loadMegaSound())
		sfxID = SeqArc::SE_OBJ_HUGE_KINO_APP;
	return SND::playSFX(sfxID, pos);
}

// BootScene::onDestroy, overlay 1, 0x020CCB28: `mov r0, #1`, ARM mode.
ncp_hook(0x020CCB28, 1)
void DSi_logoSceneEnd()
{
	if (!soundHeap)
		return;
	SND::stopSFX(SeqArc::SE_OBJ_HUGE_KINO_APP);
	releaseSoundHeap();
}
