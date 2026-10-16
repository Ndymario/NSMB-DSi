# NSMB-DSi

An [NCPatcher](https://github.com/TheGameratorT/NCPatcher) v2 module that lets
**New Super Mario Bros. (DS)** use the DSi's hardware when it is booted in DSi
mode, and changes nothing when it is not.

| | DS, or DSi in DS mode | DSi mode |
|---|---|---|
| Root heap (everything the game allocates) | 2.3MB | **7.9MB** |
| Lowest free root heap, title → 1-1 | 0.4MB | **5.9MB** |
| Extra heaps for mods (`DSi::extendedHeap`, `lowHeap`, `nwramHeap`) | none | **4.0MB + 2.3MB + 768KB** |
| ARM9 clock | 67MHz | **134MHz** (41% less CPU time per frame in 1-1) |

Figures from `tools/harness/run.py --check` on stock melonDS 1.1.

> [!NOTE]
> **Status.** Verified on stock melonDS 1.1 in DSi mode, using direct boot:
> - title screen, menus, touch, sound, saves, the opening cutscene, the world map and World 1-1 all work;
> - DS mode is unchanged.
>
> Not tested yet: Unlaunch, TWiLight Menu++ / nds-bootstrap, and real DSi/3DS
> hardware. The ROM header is unsigned, so the stock DSi menu will not launch it.

## How it works, in short

NSMB is an NTR (DS) title, so DSi launchers run it in DS mode with 4MB of RAM.
The build therefore marks the ROM as **DSi-enhanced**, which makes DSi-mode
loaders start it with 16MB of RAM and the SCFG registers unlocked. The module
then handles what the NTR SDK does not expect:
- it mirrors the launcher's boot data from `0x02FFFxxx` to `0x027FFxxx`;
- it widens the MPU;
- it moves the root heap into the upper 8MB (`0x02800000`–`0x02FE0000`);
- it turns the remaining free RAM and the DSi's new WRAM into heaps;
- it switches the ARM9 to 134MHz.

On a DS none of that runs: the module only reads three registers.
[docs/how-it-works.md](docs/how-it-works.md) has the memory map, the boot sequence and every hook.

## Building

You need CMake, a C++20 compiler, Python 3, the
[Arm GNU toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
(GCC 12+), the Nitro SDK / Nitro System headers (`nitro.h`, `nnsys.h`; not
distributed here), and a clean US ROM (`A2DE`).

```bash
tools/bootstrap.sh --nitro /path/to/dir/with/nitro.h --harness
```

```bash
cp "/path/to/New Super Mario Bros. (USA).nds" rom.nds
```

```bash
tools/ncp.sh build
```

`bootstrap.sh` builds a pinned NCPatcher v2 and fetches a pinned NSMB-Code-Reference.
It converts the SDK headers and builds the test harness, all into the
gitignored `.deps/`. It also writes `.ncpatcher.env`, so nothing touches your
shell profile. Run it again any time; it only redoes what changed.

The output is `build/nsmb-dsi.nds`. To play it in DSi mode, set melonDS to
*Console type: DSi* with your DSi BIOS, firmware and NAND, and use direct boot.
Start melonDS from a terminal to see which mode the game is in: the module
logs a summary through the no$gba debug port at boot.

```
[DSi] DSi mode: ARM9 134 MHz, root heap 0x02800000 (8175 KB)
[DSi] extra heaps: extended 3967 KB, low 2363 KB, NWRAM 767 KB
```

In DS mode it prints `[DSi] DS mode: module inactive, game unchanged`.

## Using the module in your own project

Copy `modules/dsi` into your project's `modules/` directory and enable it.
Then add the post-build hook that makes the ROM DSi-enhanced, along with
`tools/twl_header.py`. Other modules can use the API in
[`dsi/dsi.hpp`](modules/dsi/include/dsi/dsi.hpp):

```cpp
#ifdef MODULE_DSI
#include "dsi/dsi.hpp"
if (Heap* heap = DSi::extendedHeap())
    buffer = heap->allocate(size, 4);
#endif
```

See [docs/integrating.md](docs/integrating.md) for the details, the
components you can switch off, and notes on NSMB co-op.

## Components

| Component | What it does | Default |
|---|---|---|
| `Core`, `Core7` | DSi detection, boot data fix-up, DSi-BIOS sound fix, public API | always on |
| `ExtendedRam` | Root heap to `0x02800000` (7.9MB); `extendedHeap` (4MB), `lowHeap` (2.3MB) | on |
| `FastClock` | ARM9 at 134MHz; without it the core sets 67MHz | on |
| `NWram` | 768KB of new shared WRAM as `nwramHeap` | on |
| `MegaLogo` | Just for fun: in DSi mode the Nintendo logo plays the Mega Mushroom sound instead of its coin ding | on |
| `Report` | Boot and per-frame figures for the test harness | on here, off in releases |

## Testing

`tools/harness` is a headless runner built on stock melonDS. It boots the ROM in DS and DSi
mode, plays a scripted run, and reads the module's boot report out of emulated RAM:

```bash
python3 tools/harness/run.py --check
```

Add `--vs` to also play a two-console Mario vs. Luigi match over melonDS's
local wireless. It runs in real time, so it takes about two minutes.

It writes contact sheets and JSON reports to `build/harness/`, and exits non-zero if an invariant
breaks: DS mode must stay vanilla, DSi mode must get every resource, and the
DSi-mode music must match the DS-mode music (if numpy is installed), and the
mode switches between the main game, minigames and Mario vs. Luigi must land
on the same screens as in DS mode.
BIOS, firmware and NAND paths are read from your melonDS settings. The NAND is
copied first, so your dump is never written to.

## Repository layout

```
modules/dsi/          the module (module.yaml, include/dsi, source9, source7)
ncpatcher.yaml        development project: vanilla ROM + this module
tools/bootstrap.sh    pinned dependencies into .deps/
tools/ncp.sh          runs the pinned ncpatcher
tools/twl_header.py   post-build: NTR header -> DSi-enhanced header
tools/harness/        headless melonDS runner and checks
docs/                 how it works, integrating
```

The previous incarnation of this project (a runtime overlay loader, the
Ordinary port, and a patched melonDS) is preserved at tag `legacy-v0`.
None of it is needed any more.
