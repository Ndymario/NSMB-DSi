# Using the DSi module in your project

## 1. Add the module

Copy `modules/dsi` into your project's modules directory and enable it:

```yaml
# ncpatcher.yaml
modules:
  dir: modules
  enabled:
    - dsi
```

Your project needs an `arm7` target (the module patches the ARM7 too). The
`targets` block in this repository's [`ncpatcher.yaml`](../ncpatcher.yaml) is
a working example; it matches the NSMB-Code-Template / co-op layout.

## 2. Make the output DSi-enhanced

A module cannot declare build hooks, so copy `tools/twl_header.py` and add:

```yaml
hooks:
  - name: Make the ROM DSi-enhanced
    run: '"${env.NCP_PYTHON:-python3}" tools/twl_header.py "${rom.output}"'
    when: post-build
```

Without this step the module still builds and runs, but nothing boots the
game in DSi mode, so it stays a no-op. `tools/twl_header.py --check ROM`
verifies a finished ROM.

## 3. Choose components

All components are on by default. Switch any of them off, or move a heap's
bounds, from the project:

```yaml
modules:
  enabled:
    - dsi:
        components:
          Report: { enabled: false }          # always, for releases
          NWram: { enabled: false }
          ExtendedRam:
            defines: [DSI_ROOT_HEAP_END=0x02F00000]   # leave 1MB below the system area
```

| Component | Requires | Provides |
|---|---|---|
| `Core`, `Core7` | | Detection, boot fix-ups, `isConsole`, `isTwlMode`, `setFastClock` |
| `ExtendedRam` | Core | Root heap moved to `0x02800000`; `extendedHeap`, `lowHeap` |
| `FastClock` | Core | ARM9 at 134MHz at boot (without it: 67MHz) |
| `NWram` | Core | `nwramHeap`; MPU region 1 widened to 32MB |
| `Report` | Core | Boot report and per-frame figures for `tools/harness` |

## 4. Use the API

```cpp
#ifdef MODULE_DSI
#include "dsi/dsi.hpp"
#endif

void* allocateBigBuffer(u32 size)
{
#ifdef MODULE_DSI
	if (Heap* heap = DSi::extendedHeap())
		if (void* p = heap->allocate(size, 4))
			return p;
#endif
	return Memory::rootHeapPtr->allocate(size, 4); // DS mode: the root heap
}
```

Rules:

- **Every resource is optional.** On a DS, on a DSi in DS mode, or with a
  component switched off, heaps are `nullptr` and `isTwlMode()` is false. Code
  that needs DSi resources must have a DS path.
- **Ask after boot.** The answers are settled before the first heap exists. Any
  game code, including constructors of scenes and actors, can rely on them.
- **The clock can change.** `DSi::setFastClock(bool)` switches between 67MHz
  and 134MHz at any time (for example from an options menu). It returns false
  outside DSi mode.
- **Plain `new` already benefits.** The root heap is 3.5x larger in DSi mode,
  so most mods get more headroom without calling the API at all.

## NSMB co-op

The co-op module's `DSiModeWarn` and this module don't touch the same
addresses:
- **DSiModeWarn** hooks ARM7 `0x037FCEEC`, overlay 1 BootScene, and the shared
  word `0x027FFA7E`.
- **This module** hooks ARM7 `0x037F846C`, the five ARM9 `main0` sites in
  [how-it-works.md](how-it-works.md), and the shared block `0x027FFA40-0x027FFA5F`.

One catch: DSiModeWarn decides "DS CPU mode" by checking for an nds-bootstrap
patch, so in DSi mode it would still warn even though the ARM9 runs at 134MHz.
When both are present, gate the warning on `DSi::isFastClock()`.
This combination has not been built or tested yet.
