# How it works

## The problem

NSMB is built with the NTR (DS) SDK, so it has no idea the DSi exists. A DSi
runs an NTR cartridge in **DS mode**: 4MB of RAM (mirrored every 4MB), SCFG
locked, 67MHz. Nothing the game does can get out of that. Changing that takes
two things:

1. **Get booted in DSi mode.** NCPatcher's `rom.dsi-enhanced` marks the ROM as
   DSi-enhanced (unit code `02h`, plus a DSi extended header). A DSi-mode loader
   then keeps the console in DSi mode: 16MB of unmirrored RAM, SCFG and MBK
   registers accessible, NWRAM mapped from the header. The module's code lives
   in the normal binaries; the ARM9i/ARM7i binaries are 1KB inert stubs that
   exist only because TWiLight Menu++ rejects DSi-enhanced ROMs without them.
   On a real DSi the usual way in is different: TWiLight Menu++ runs a copy
   with the plain DS header through nds-bootstrap in forced DSi mode (see
   [Running from the SD card](#running-from-the-sd-card-twilight-menu--nds-bootstrap)).
2. **Keep NTR code working there.** That is what the module's core does, and it
   is small, because the NTR SDK turns out to cope with 16MB well once a few
   assumptions are fixed up.

## Boot sequence

| When | CPU | Where | What the module does |
|---|---|---|---|
| Before ARM7 `OS_Init` | ARM7 | `0x037F846C` NitroSpMain: `bl OS_Init` | Detects DSi hardware (`SNDEXCNT`) and DSi mode (`SCFG_EXT7`). In DSi mode it switches the ARM7's revised/extended circuits back to NITRO behaviour, copies the launcher's boot data from `0x02FFFxxx` to `0x027FFxxx` (cold boot only), then publishes what it found in the shared block at `0x027FFC24`. Under a loader it skips the first two (see below). |
| First game code | ARM9 | `0x020056A8` App::branchToStartup (reached from crt0's NitroStartUp, before OS_Init and NitroMain) | Detects DSi mode: `SCFG_A9ROM = 1`, `SCFG_EXT9` access bit set, RAM limit 16MB. In DSi mode it widens MPU region 1 to at least 16MB, then, unless a loader set DSi mode up, switches the ARM9's revised/extended circuits back to NITRO behaviour and copies the boot data as well. Both CPUs copy, so neither has to wait for the other. |
| After `OS_Init` | ARM9 | `0x02004E44` App::commonStartup: `bl Memory::initRootHeap` | Reads the ARM7's shared block. The PXI handshake guarantees it is filled in by now. Maps NWRAM, re-asserts region 1 (OS_Init shrinks it to 4MB), sets the clock, and points the main arena at `0x02800000-0x02FE0000`. Under a loader only region 1 is re-asserted. |
| After the root heap exists | ARM9 | `0x02004E48` App::commonStartup: `bl 0x02012764` (empty) | Creates `extendedHeap`, `lowHeap` and `nwramHeap`; fills in the report. |
| Every pitch lookup | ARM7 | `0x037FF5A0` `_Ven_SVC_GetPitchTable` (replaced) | Works around the DSi BIOS `GetPitchTable` bug in DSi mode (see below). |
| Nintendo logo (MegaLogo only) | ARM9 | ov1 `0x020CCDDC` BootScene: `bl SND::playSFX` (logo ding), ov1 `0x020CCB28` `BootScene::onDestroy` | In DSi mode, loads the Mega Mushroom's sound group into a 1MB NNS sound heap borrowed from `extendedHeap` (from the root heap under nds-bootstrap) and plays it instead of the ding. Destroys the heap when the logo scene ends, which clears the archive's file pointers before the title screen loads its sounds. |
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
`OS_Init` fills those in on its own, and so is `0x027FFC24-0x027FFC2B`, which
the SDK leaves unused and the module uses for its shared block.

The copy happens only on a **cold** boot. NSMB switches between the main
game, Mario vs. Luigi and the minigames with `OS_ResetSystem`. It stores the
mode to boot into in the reset parameter at `0x027FFC20`, and multiboot info
lives at `0x027FFC40`. Copying again after such a reset would put back the
launcher's values: the minigames then rebooted to the Nintendo logo. After a
reset the NTR ROM header already matches the TWL one (it is the last range
copied and nothing else writes it), so `mirrorBootArea()` checks that and
does nothing.

### Why the extended circuits are switched off

The launcher starts a DSi-enhanced title with every DSi improvement to the old
hardware switched on: `SCFG_EXT9 = 8307F100h`, and `SCFG_EXT7` from the header.
NTR code was written against the NITRO behaviour. With "extended VRAM
access", for example, 8-bit writes to VRAM take effect, while a DS drops them.
So the core clears those bits and keeps only access to the new resources:
- ARM9 `SCFG_EXT9` bits 0-4, 7, 8, 12 and 13, which leaves `8307C000h`;
- ARM7 `SCFG_EXT7` bits 0-2, 8, 9 and 10.

The header asks for NITRO behaviour on the ARM7 too, for loaders that honour
it.

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
| `0x02800000-0x02FE0000` | 7.9MB | **root heap** |
| `0x02FE0000-0x02FFBFFF` | 112KB | DSi launcher data, left alone (see below) |
| `0x02FFC000-0x02FFFFFF` | 16KB | DSi system area (launcher boot data) |
| `0x03000000-0x030BFFFF` | 768KB | **`DSi::nwramHeap()`** (NWRAM-A/B/C, ARM9 only) |

The root heap stops 112KB short of the system area on purpose. DSi software
reads launcher data there, and stock melonDS fakes one word of it:
`DSi::ARM9Read32` always returns `FFFFFFFF` for `0x02FE71B0`, a region-lock
bypass. An earlier version ran the heap to `0x02FFC000`. During a Mario vs.
Luigi match a compressed Grassland tile file landed on that word, and 4 bytes
of `FF` turned into the stray background tiles. `run.py --check` now asserts
the heap ends at or below `0x02FE0000`, and `--check --vs` replays the match.

The ExtendedRam component's defines (`DSI_ROOT_HEAP_START` and friends) move
these bounds.

In DS mode none of this applies: the memory map, MPU and heaps are vanilla.
Under nds-bootstrap the map is nds-bootstrap's, described below.

### The shared block

The ARM7 learns things the ARM9 cannot see (DSi hardware in DS mode shows only
through the ARM7's `SNDEXCNT`), so it leaves 8 bytes for the ARM9 at
`0x027FFC24-0x027FFC2B`: magic `D7h`, flags, `SNDEXCNT`, and `SCFG_EXT7` as it
found it. The Report component then reuses the last word for its own address.
The NTR SDK marks this range unused. Its other unused range,
`0x027FFA00-0x027FFA7F`, where the block used to be, is not free in practice:
nds-bootstrap keeps its card engine's shared state at `0x027FFA0C` and a table
of the code it patched at `0x027FFA40`, and NSMB co-op uses `0x027FFA7E`.

## MPU

crt0 sets region 1 to 8MB and `OS_Init` shrinks it to 4MB (`0x0200002B`). In
DSi mode the module keeps it at `0x02000031`: base `0x02000000`, 32MB, cached.
It only ever widens region 1: nds-bootstrap starts the game with it at 32MB,
because its card engine reads file tables it keeps at `0x03700000`, and
narrowing it to 16MB made that engine take a data abort.
That is the same value the DSi launcher uses for TWL titles, and it covers both
main RAM and the NWRAM window. If the NWram component is off, the module uses
16MB (`0x0200002F`) instead. Every other region is left as the SDK set it.
That includes region 2, which keeps `0x023E0000-0x023FFFFF` inaccessible, so
stray NTR-era aliases into that range still fault instead of corrupting a heap.

## The DSi-enhanced header

`rom.dsi-enhanced` sets the following fields (GBATEK, "DSi Cartridge Header"):

| Field | Value | Why |
|---|---|---|
| `[012h]` unit code | `02h` | NDS + DSi |
| `[01Ch]` | `01h` | Header has a TWL region, so ARM9i/ARM7i entries count |
| `[180h-1AFh]` MBK1-9, WRAMCNT | NWRAM to ARM9 at `0x03000000`; MBK9 = 0; WRAMCNT = 3 | Same layout the NWram component re-applies |
| `[1B0h]` regions | `FFFFFFFFh` | Region free |
| `[1B8h]` SCFG_EXT7 | bit31 only | Keep SCFG/MBK access; keep the NITRO DMA and sound circuits the NTR ARM7 code expects |
| `[1BFh]` flags | `00h` | Touchscreen/sound controller in **NDS mode**: the NTR ARM7 code cannot drive DSi mode |
| ARM9i / ARM7i | 400h-byte stubs at the start of the TWL area, loaded to `0x02400000` / `0x02E80000` | Never run. TWiLight Menu++ refuses the ROM ("The DSi binaries are missing") unless both offsets are ≥ `8000h` and their first 16 bytes are not all `00h`, all `FFh` or a copy of the bytes at `8000h`; `--check` applies the same rule. The heaps later reuse both RAM areas |
| `[210h]` total used | end of the stubs | Covers the TWL area |
| `[230h]` title ID | game code reversed, `00 00 03 00` | Cartridge, DSi |

The game code stays `A2DE`, so saves, cheats and per-game loader settings carry
over. The header is not signed.

`rom.ds-copy` also writes `build/nsmb-dsi-twilight.nds` first: the ROM exactly
as NCPatcher built it, with the plain DS header. That is the file for
TWiLight Menu++.

## Running from the SD card: TWiLight Menu++ / nds-bootstrap

On a DSi with Unlaunch, TWiLight Menu++ runs games from the SD card through
nds-bootstrap, which patches the game's card reads to read the SD card
instead. Two things about nds-bootstrap (checked in its source, v2.17.0)
decide how this module can run under it:

- **It cannot run the DSi-enhanced ROM in DSi mode.** For a unit code above
  0 it assumes a DSi-SDK title and insists on patching a DSi-SDK card hash
  function NSMB does not have ("Card hash init not found", then "An error has
  occurred"). For a plain DS ROM with the game's "Run in" setting at
  **DSi mode (forced)** it does keep the console in DSi mode: SCFG unlocked,
  16MB, 134MHz. Hence `nsmb-dsi-twilight.nds`.
- **It needs most of the DSi's extras itself.** It keeps an 8MB cache of the
  ROM at `0x02800000-0x02FFF000` (through the `0x0C000000` mirror), its card
  engine and cache tables just below `0x02800000`, and the ROM's file tables in
  NWRAM at `0x03700000`. It sets the clock itself and patches the game's delay
  loops for it. And it patches the game's MPU setup (region 1 at 32MB, no
  region 2 guard), so the NTR SDK takes its 8MB layout and the root heap
  already runs from `0x0219xxxx` to `0x027C0000`, about 6.3MB.

So the core recognises a loader and stays out of its way. The DSi launcher
never starts a DS-header game in DSi mode, so DSi mode with a valid header of
unit code 0 at `0x027FFE00` (CRC checked) means a loader set things up
(`startedByLoader()` in `dsi/shared.hpp`, `DSi::isLoaderBoot()`). Started by
the launcher, that area holds stale data on a cold boot, whose CRC does not
match, or our own DSi-enhanced header after a soft reset. Under a loader:

| | Launcher (melonDS direct boot, unsigned cart) | nds-bootstrap, DSi mode (forced) |
|---|---|---|
| Root heap | moved to `0x02800000-0x02FE0000`, 7.9MB | left where the SDK put it, about 6.3MB |
| `extendedHeap` / `lowHeap` | 3.9MB / 2.3MB | none |
| NWRAM (`nwramHeap`) | 768KB | none, nds-bootstrap uses it |
| ARM9 clock | 134MHz, set by the module | 134MHz, set by nds-bootstrap; `setFastClock` refuses |
| SCFG NITRO bits, boot-data copy | done by the module | left to nds-bootstrap |
| GetPitchTable fix | the module's | the module's (nds-bootstrap's own fix looks for the veneer the module replaces, so exactly one applies) |

So under nds-bootstrap the game gets about 2.7 times its usual memory and twice
the clock, against 3.4 times the memory, spare heaps and NWRAM when booted
directly.

### Testing it in melonDS

`tools/harness` can run nds-bootstrap from a folder used as the DSi's SD card
(`--sd DIR --argv sd:/_nds/nds-bootstrap-release.nds`, booting
`nds-bootstrap-release.nds` with `--rom`). On stock melonDS (1.1, and master
as of 2026-08) nds-bootstrap stalls on its first NDMA read from the SD card,
even with the unmodified game: its wait loop needs `SD_IRQ_STATUS` bit 30
(read at `0x0400481E`, bit 14), which melonDS masks off. With that bit modelled
locally (set after each command write until it is read once; not committed,
since this project uses stock melonDS), the TWiLight build boots in forced DSi
mode, plays the 1-1 scenario, plays the Mega Mushroom sound at the logo, and its
title music matches DS mode (spectral similarity 0.99999), with the report as
in the table above. So it is not part of `run.py --check`.

## What was measured

All measured with `tools/harness` on stock melonDS 1.1 (interpreter, direct boot):

- **Gameplay works in DSi mode.** The run covers title, menus, a new file, the
  intro, the world map, entering 1-1, playing, dying and going back to the map.
  Touch input and save writes behave as in DS mode.
- **Sound matches DS mode.** The title music's averaged spectrum has 0.999
  correlation with the DS run, with the same dominant pitches. Before the
  pitch-table fix it was 0.36.
- **RAM.** The root heap grows from 2.3MB to 7.9MB. Its lowest free size during
  the run goes from 0.37MB to 5.9MB.
- **Two-player Mario vs. Luigi** over local wireless, both consoles in DSi
  mode, connects, sets up and plays Grassland. The background is
  pixel-identical to DS mode. The check renders that frame without sprites
  and 3D (`--shot-bg`): the Big Star spawns at a random spot, which any timing
  change moves, while the background must not change at all. It still catches
  the old `0x02FFC000` heap end (79 pixels).
- **Clock.** Median CPU time per frame standing in 1-1 is 2046 ticks (3.9ms) in
  DS mode and 2048 in DSi mode at 67MHz, so DSi mode by itself changes nothing.
  At 134MHz it is 1216 ticks (2.3ms), 41% less; the rest is memory-bound.

## Open questions

- **Real hardware.** A DSi XL runs the TWiLight build in DSi mode under
  nds-bootstrap v2.17.0 (logo sound confirms it; played by hand). Not yet tried
  on a 3DS, or with the DSi-enhanced ROM on real hardware. How a real launcher
  handles the header's MBK and SCFG_EXT7 fields is unknown: melonDS's direct
  boot ignores `[1B8h]` and leaves SCFG_EXT7 at the firmware default.
- **Other loaders.** Under a loader other than nds-bootstrap the module adds
  nothing (it does not know what the loader uses), which is safe but untested.
- **Wi-Fi in DSi mode.** Not exercised. DS Download Play writes to
  `0x023FE800`, which stays guarded by MPU region 2 on the ARM9 side.
