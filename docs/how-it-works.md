# How it works

## The problem

NSMB is built with the NTR (DS) SDK, so it has no idea the DSi exists. A DSi
runs an NTR cartridge in **DS mode**: 4MB of RAM (mirrored every 4MB), SCFG
locked, 67MHz. Nothing the game does can get out of that. Changing that takes
two things:

1. **Get booted in DSi mode.** `tools/twl_header.py` marks the ROM as
   DSi-enhanced (unit code `02h`, plus a DSi extended header). A DSi-mode loader
   then keeps the console in DSi mode: 16MB of unmirrored RAM, SCFG and MBK
   registers accessible, NWRAM mapped from the header. There is no ARM9i/ARM7i
   payload; the module's code lives in the normal binaries.
2. **Keep NTR code working there.** That is what the module's core does, and it
   is small, because the NTR SDK turns out to cope with 16MB well once a few
   assumptions are fixed up.

## Boot sequence

| When | CPU | Where | What the module does |
|---|---|---|---|
| Before ARM7 `OS_Init` | ARM7 | `0x037F846C` NitroSpMain: `bl OS_Init` | Detects DSi hardware (`SNDEXCNT`) and DSi mode (`SCFG_EXT7`). In DSi mode it copies the launcher's boot data from `0x02FFFxxx` to `0x027FFxxx`, then publishes what it found in the shared block at `0x027FFA40`. |
| First game code | ARM9 | `0x020056A8` App::branchToStartup (reached from crt0's NitroStartUp, before OS_Init and NitroMain) | Detects DSi mode: `SCFG_A9ROM = 1`, `SCFG_EXT9` access bit set, RAM limit 16MB. In DSi mode it widens MPU region 1 to 16MB and copies the boot data as well. Both CPUs copy, so neither has to wait for the other. |
| After `OS_Init` | ARM9 | `0x02004E44` App::commonStartup: `bl Memory::initRootHeap` | Reads the ARM7's shared block. The PXI handshake guarantees it is filled in by now. Maps NWRAM, re-asserts region 1 (OS_Init shrinks it to 4MB), sets the clock, and points the main arena at `0x02800000-0x02FFC000`. |
| After the root heap exists | ARM9 | `0x02004E48` App::commonStartup: `bl 0x02012764` (empty) | Creates `extendedHeap`, `lowHeap` and `nwramHeap`; fills in the report. |
| Every pitch lookup | ARM7 | `0x037FF5A0` `_Ven_SVC_GetPitchTable` (replaced) | Works around the DSi BIOS `GetPitchTable` bug in DSi mode (see below). |
| Nintendo logo (MegaLogo only) | ARM9 | ov1 `0x020CCDDC` BootScene: `bl SND::playSFX` (logo ding), ov1 `0x020CCB28` `BootScene::onDestroy` | In DSi mode, loads the Mega Mushroom's sound group into a 1MB NNS sound heap borrowed from `extendedHeap` and plays it instead of the ding. Destroys the heap when the logo scene ends, which clears the archive's file pointers before the title screen loads its sounds. |
| Every frame (Report only) | ARM9 | `0x02004F14` / `0x02004F18` App::gameLoop: around `waitVBlankIntr` | Samples heap free sizes and the CPU time spent per frame. |

Every patch is on ARM-mode code. All are in `main0` or ARM7 main except
MegaLogo's two, which are in overlay 1. All are `ncp_hook`s except the
pitch-table veneer (`ncp_jump`) and the logo sound call (`ncp_call`). The addresses come from disassembling the clean US binaries, named with
symbols from NSMB-Code-Reference `ac82391`. Each was then confirmed at runtime
by the harness: hook order, register values, and the report contents.

### Why the boot data has to be copied

For a DSi-mode title the launcher writes the cartridge header, boot indicator,
user settings (nickname, language, touch calibration) and card ID to the TWL
system area at `0x02FFFxxx`. The NTR SDK reads them at `0x027FFxxx`. On a DS
those two addresses are the same RAM, through the 4MB mirror; in 16MB mode they
are not. Without the copy the file system has no ROM header and the game cannot
load anything. The ranges are listed in
[`dsi/shared.hpp`](../modules/dsi/include/dsi/shared.hpp). The arena table and
exception vector at `0x027FFD80-0x027FFDFF` are deliberately skipped, because
`OS_Init` fills those in on its own.

### Why the ARM7 needs a BIOS fix

In DSi mode the ARM7 runs the DSi BIOS instead of the NDS one. GBATEK lists
its differences. NSMB's ARM7 has wrappers for every SWI, but only two have
callers: `GetVolumeTable` (unchanged on DSi) and `GetPitchTable`. On the
DSi7 BIOS `GetPitchTable` is bugged: it reads the sine table. The sound
driver's `SND_CalcTimer` uses it for every note, so the music came out at the
wrong pitches. Measured, the dominant notes dropped from 287–498Hz to 64–223Hz.

The module replaces the ARM-to-THUMB veneer that calls the wrapper. In DSi mode
it subtracts `46Ah` from the index (GBATEK's workaround), then issues the SWI
itself. In DS mode the index passes through untouched. `run.py --check` now
compares the title music's spectrum between the DS and DSi runs.

### Why the root heap moves

`Memory::initRootHeap` turns the whole main arena into the root heap, and
nearly every allocation in the game comes from it. The module checked main and
all 131 overlays: `initRootHeap` is the only reader of the main arena. So
pointing the arena at the largest free block is the whole change. There are no
allocator hooks, no file-system hooks, and no special cases.

## Memory map in DSi mode

| Range | Size | Owner |
|---|---|---|
| `0x02000000-0x0219xxxx` | | ARM9 binary, overlays, module code |
| `0x0219xxxx-0x023E0000` | 2.3MB | **`DSi::lowHeap()`** (the vanilla root heap's home) |
| `0x023E0000-0x02400000` | 128KB | MPU region 2 guard (NTR SDK), left alone |
| `0x02400000-0x027E0000` | 3.9MB | **`DSi::extendedHeap()`** |
| `0x027E0000-0x02800000` | 128KB | ARM7 code/data, ARM9 DTCM, NTR shared area |
| `0x02800000-0x02FFC000` | 8.0MB | **root heap** |
| `0x02FFC000-0x02FFFFFF` | 16KB | DSi system area (launcher boot data) |
| `0x03000000-0x030BFFFF` | 768KB | **`DSi::nwramHeap()`** (NWRAM-A/B/C, ARM9 only) |

The ExtendedRam component's defines (`DSI_ROOT_HEAP_START` and friends) move
these bounds, for example to leave room for a loader that needs part of upper RAM.

In DS mode none of this applies: the memory map, MPU and heaps are vanilla.

## MPU

crt0 sets region 1 to 8MB and `OS_Init` shrinks it to 4MB (`0x0200002B`). In
DSi mode the module keeps it at `0x02000031`: base `0x02000000`, 32MB, cached.
That is the same value the DSi launcher uses for TWL titles, and it covers both
main RAM and the NWRAM window. If the NWram component is off, the module uses
16MB (`0x0200002F`) instead. Every other region is left as the SDK set it.
That includes region 2, which keeps `0x023E0000-0x023FFFFF` inaccessible, so
stray NTR-era aliases into that range still fault instead of corrupting a heap.

## The DSi-enhanced header

`tools/twl_header.py` sets the following fields (GBATEK, "DSi Cartridge Header"):

| Field | Value | Why |
|---|---|---|
| `[012h]` unit code | `02h` | NDS + DSi |
| `[01Ch]` | `01h` | Header has a TWL region, so ARM9i/ARM7i entries count |
| `[180h-1AFh]` MBK1-9, WRAMCNT | NWRAM to ARM9 at `0x03000000`; MBK9 = 0; WRAMCNT = 3 | Same layout the NWram component re-applies |
| `[1B0h]` regions | `FFFFFFFFh` | Region free |
| `[1B8h]` SCFG_EXT7 | bit31 only | Keep SCFG/MBK access; keep the NITRO DMA and sound circuits the NTR ARM7 code expects |
| `[1BFh]` flags | `00h` | Touchscreen/sound controller in **NDS mode**: the NTR ARM7 code cannot drive DSi mode |
| ARM9i / ARM7i | size 0 at `0x02400000` / `0x02E80000` | No DSi-only binaries |
| `[230h]` title ID | game code reversed, `00 00 03 00` | Cartridge, DSi |

The game code stays `A2DE`, so saves, cheats and per-game loader settings carry
over. The header is not signed.

## What was measured

All measured with `tools/harness` on stock melonDS 1.1 (interpreter, direct boot):

- **Gameplay works in DSi mode.** The run covers title, menus, a new file, the
  intro, the world map, entering 1-1, playing, dying and going back to the map.
  Touch input and save writes behave as in DS mode.
- **Sound matches DS mode.** The title music's averaged spectrum has 0.999
  correlation with the DS run, with the same dominant pitches. Before the
  pitch-table fix it was 0.36.
- **RAM.** The root heap grows from 2.3MB to 8.0MB. Its lowest free size during
  the run goes from 0.37MB to 6.0MB.
- **Clock.** Median CPU time per frame standing in 1-1 is 2046 ticks (3.9ms) in
  DS mode and 2048 in DSi mode at 67MHz, so DSi mode by itself changes nothing.
  At 134MHz it is 1216 ticks (2.3ms), 41% less; the rest is memory-bound.

## Open questions

- **Real loaders.** It is not yet known how Unlaunch, TWiLight Menu++ /
  nds-bootstrap and hardware handle the header, especially MBK and SCFG_EXT7.
  melonDS's direct boot ignores `[1B8h]` and leaves SCFG_EXT7 at the firmware
  default. nds-bootstrap may also claim part of upper RAM.
- **Wi-Fi in DSi mode.** Not exercised. DS Download Play writes to
  `0x023FE800`, which stays guarded by MPU region 2 on the ARM9 side.
