# AGENTS.md — NSMB DS Code Modding (NCPatcher / NSMB-Docker)

This workspace is for **New Super Mario Bros. (Nintendo DS)** code mods using  
**NSMB-Docker + NCPatcher**.

Agents must treat this repo as **code-first**: ARM9 hooks, jumps, patches, and
NitroFS file replacement. Do not commit ROMs or attempt manual binary edits.

---

## Core Principles (Read First)
- **All builds go through NSMB-Docker**
- **All code is compiled with NCPatcher**
- **Addresses, hooks, and jumps must be verified in Ghidra**
- No ROMs, no raw base-game assets committed
- Small, deterministic changes only

If something is unclear, **do not guess offsets** — consult Ghidra.

---

## Toolchain Overview

### Build System
- **NSMB-Docker** is the canonical build environment
- Under the hood:
  - Uses **NCPatcher** to compile and inject code
  - Uses a clean ROM stored in a persistent Docker volume
  - Handles NitroSDK conversion automatically

### Emulator
- **melonDS**
```bash
/Applications/melonDS.app/Contents/MacOS/melonDS final.nds
```

---

## Canonical Build Command (Required)

This is the *only* supported build command:

```bash
docker run --rm -it \
  -v ./source:/app/source \
  -v ./nitrofs:/app/nitrofs \
  -v "$PWD:/workspace" \
  -v nsmb-data:/data \
  nsmb-docker
```

**Never invent alternate build steps.**

---

## Repository Layout (Host)

```
.
├── source/          # C/C++ source compiled by NCPatcher
├── nitrofs/         # Files injected into the ROM filesystem
│   └── z_new/
│       └── <mod>/   # New files only (do not overwrite base paths here)
├── arm9.json        # NCPatcher config (optional override)
├── ncpatcher.json   # NCPatcher config (optional override)
├── buildrules.txt   # NCPatcher rules (rarely overridden)
└── AGENTS.md
```

### `source/`
- ARM9 code mods
- Hooks, jumps, replacements
- Uses NCPatcher macros and conventions

### `nitrofs/`
- File replacement / injection
- **Replacing files**: mirror original NitroFS path + filename
- **Adding files**: must go in `z_new/<mod_name>/`

---

## Docker Container Layout (Reference)

Inside the container:

```
/workspace/        # Mounted project root ($PWD)
/app/              # Working environment
/app/scripts/      # NSMB-Docker scripts
/app/arm9.json     # Default ARM9 config
/app/ncpatcher.json
/data/             # Persistent volume
/data/include/     # Converted NitroSDK + Nitro System
/data/nsmb.nds     # Clean base ROM (never committed)
/opt/
├── NCPatcher/              # Built NCPatcher (on PATH)
└── NSMB-Code-Reference/    # Code template (nsmb.hpp) + symbols (symbols9.x / symbols7.x)
```

---

## Code Compilation & Patching

### Compiler
- **NCPatcher**
- `ncprt` is **not implemented** — do not reference or rely on it


### Patch Types (NCPatcher)

Agents must choose the correct patch primitive. Misuse will cause crashes or subtle corruption.

#### `ncp_hook`
- Replaces the instruction at the specified address with a **safe call** to your function.
- Conceptually expands to:
  ```asm
  PUSH {R0-R3, R12, LR}
  BL   yourFunction
  POP  {R0-R3, R12, LR}
  replacedInstruction
  B    patchedAddress + 4
  ```
- Guarantees that **no registers are modified** from the perspective of the original function.
- Automatically executes the original replaced instruction.
- **Cannot hook LDR instructions** (current limitation).

**Use when:**
- You want to run custom logic when execution reaches an address.
- You must not disturb the original function’s state.
- You are observing, logging, or conditionally modifying behavior.

---

#### `ncp_jump`
- Replaces the instruction at the specified address with a **jump**:
  ```asm
  B yourFunction
  ```
- **Does not return** to the original code.
- Does **not** preserve registers automatically.

**Use when:**
- You want to **replace the entire function**.
- Applied at the **first instruction** of a function.
- You fully control register usage and function behavior.

---

#### `ncp_call`
- Replaces the instruction at the specified address with a **call**:
  ```asm
  BL yourFunction
  ```
- **Returns** to the instruction following the patched one.
- Does **not** preserve registers automatically.

**Use when:**
- Replacing or intercepting a function call made by another function.
- Patching an existing `BL` instruction.
- You are responsible for preserving any required registers.

---


### Choosing the Right Patch
- Default to **`ncp_hook`** when safety matters.
- Use **`ncp_call`** when modifying call sites.
- Use **`ncp_jump`** only when fully replacing logic and you understand the calling context.

## Examples from Public NCPatcher Mods

Below are real usage patterns from public NSMB DS mods that use NCPatcher.
These illustrate when and how to use hooks, jumps, or calls safely in real projects.

### NSMB‑Coop (Full Cooperative Multiplayer)

**NSMB‑Coop** is a comprehensive mod that adds cooperative multiplayer, respawn logic,
world map adaptations, and other major gameplay features entirely through NCPatcher
code patches. It serves as a strong real‑world reference for large‑scale code mods.

Repository:
https://github.com/ShaneDoyle/nsmb-coop

#### Safe Hook Example

This pattern shows a safe hook into an existing update or event routine:

```cpp
// Example: Safe hook into a player update routine
void customPlayerUpdate(int playerId) {
    // custom cooperative logic here
}

// Hook at the original player update entry point
// Address must be converted to an in-game offset using overlay math
ncp_hook(0x0205F2A0, customPlayerUpdate);
```

- `ncp_hook` preserves registers and executes the replaced instruction.
- Use this to extend or observe existing behavior without breaking state.

---

#### Function Replacement Example

For logic that must be fully overridden (for example, boss logic):

```cpp
// Entire replacement of a boss routine
void customBossHandler() {
    // new boss behavior
}

// Jump at the first instruction of the original function
ncp_jump(0x0208A4C0, customBossHandler);
```

- `ncp_jump` does not return to original code.
- Use only when replacing an entire function intentionally.

---

#### Intercepting Calls Example

For modifying a specific function call site:

```cpp
// Logic to run before a specific function call
void preCallBehavior() {
    // inspect or modify parameters
}

// Replace an existing BL instruction
ncp_call(0x0201C320, preCallBehavior);
```

- `ncp_call` returns to the original caller after execution.
- You are responsible for saving/restoring any registers you modify.

---

### NSMB E3 2005 Demo Recreation (Historic Hack)

The **NSMB E3 2005 Demo Recreation** project aims to recreate the E3 2005 demo
version of New Super Mario Bros. using a combination of data changes and
NCPatcher-based code mods. It is a useful reference for studying behavior
differences and legacy logic.

Repository:
https://github.com/mariomadproductions/nsmb-e3-rec

This project demonstrates how code patches can be layered to reproduce
alternate gameplay behavior that does not exist in the retail game.

---

### Notes for Agents

- Always verify overlay IDs and calculate correct in-game offsets using Ghidra.
- Never copy raw addresses from example projects without recalculating them.
- Large mods like NSMB‑Coop demonstrate good organization for managing many patches.

### Address Rules (Ghidra → In-Game)
- All addresses **must** come from Ghidra.
- The **in-game offset is not the same as the Ghidra address** for overlay code.
- To get an **in-game offset**, you must adjust the Ghidra address by the overlay base using the table below.

### main0 vs Overlay IDs (Important)
- **main0 (ARM9 main)**: If the code is in `main0`, **do not provide an overlay ID argument at all**.
  - Correct: `ncp_hook(0x0201E744)`
  - Incorrect: `ncp_hook(0x0201E744, 0)` (do not pass `0`)
- Only provide an overlay ID when the code is actually in an overlay (e.g. `ncp_hook(addr, 12)`).

#### How to convert
1. Determine which **overlay ID** the code belongs to (from Ghidra / project context).
2. Look up the overlay base from the **Overlay Base Table**.
3. Compute:

   **in_game_offset = ghidra_address - overlay_base[overlay_id]**

#### Worked Example

Example scenario:
- Ghidra function address: `0x02123E40`
- Overlay ID: `81`
- Overlay base (from table): `0x00123E00`

Calculation:
```
0x02123E40 - 0x00123E00 = 0x02000040
```

Result:
- **In-game offset:** `0x02000040`

Use this **in-game offset** for `ncp_hook`, `ncp_jump`, or other NCPatcher patch directives.
Never use the raw Ghidra address directly for overlay code.

Notes:
- Overlay `0` uses base `0x00000000` (no adjustment).
- Many overlays are `0x00000000` (unknown / unused in this table); if the overlay base is `0`, treat the conversion as "not applicable" and verify the overlay mapping in Ghidra before patching.
- Always confirm **ARM vs THUMB mode** and document the symbol/function name when possible.

#### Overlay Base Table
```c
// overlay_base[overlay_id]
static const uint32_t overlay_base[] = {
    0x00000000, //  0
    0x000BA380, //  1
    0x000BCC80, //  2
    0x000BDCC0, //  3
    0x000BDCE0, //  4
    0x000BE7E0, //  5
    0x000BF2A0, //  6
    0x000BF600, //  7
    0x000BF9C0, //  8
    0x000E4DE0, //  9
    0x00000000, // 10
    0x00000000, // 11
    0x00000000, // 12
    0x000916C0, // 13
    0x0009DCC0, // 14
    0x000A15E0, // 15
    0x000A6540, // 16
    0x000AA8A0, // 17
    0x000AEE60, // 18
    0x000B1B80, // 19
    0x000B9840, // 20
    0x000BD4C0, // 21
    0x00000000, // 22
    0x000B0EE0, // 23
    0x000B3E80, // 24
    0x000B7A60, // 25
    0x000BD9E0, // 26
    0x000BF480, // 27
    0x000C03C0, // 28
    0x000C5AA0, // 29
    0x000C5AC0, // 30
    0x000C5AE0, // 31
    0x000BFB80, // 32
    0x000C1820, // 33
    0x000C41A0, // 34
    0x000C7860, // 35
    0x000C9720, // 36
    0x000CB1A0, // 37
    0x000CB1C0, // 38
    0x000CC900, // 39
    0x000CC920, // 40
    0x000D1700, // 41
    0x000CC940, // 42
    0x000D8520, // 43
    0x000DA500, // 44
    0x000DA520, // 45
    0x000DA540, // 46
    0x000DBFA0, // 47
    0x000DD760, // 48
    0x000DF6A0, // 49
    0x000E0B60, // 50
    0x000E23E0, // 51
    0x000D6820, // 52
    0x000D6820, // 53
    0x00000000, // 54
    0x000EB640, // 55
    0x00104680, // 56
    0x00107960, // 57
    0x0010AB20, // 58
    0x0010BD40, // 59
    0x0010E140, // 60
    0x0010F080, // 61
    0x0010F0A0, // 62
    0x0010F0C0, // 63
    0x0010F0E0, // 64
    0x0010F100, // 65
    0x0010BE40, // 66
    0x0010F0E0, // 67
    0x00110000, // 68
    0x00113BA0, // 69
    0x001167E0, // 70
    0x001186C0, // 71
    0x0011C6E0, // 72
    0x0011C700, // 73
    0x0011C720, // 74
    0x0011C740, // 75
    0x00118740, // 76
    0x00118760, // 77
    0x0011B5C0, // 78
    0x0011D200, // 79
    0x0011FC60, // 80
    0x00123E00, // 81
    0x00125E00, // 82
    0x00128020, // 83
    0x00128040, // 84
    0x00128060, // 85
    0x00123EE0, // 86
    0x00125160, // 87
    0x00126FC0, // 88
    0x00000000, // 89
    0x0012BB80, // 90
    0x00133620, // 91
    0x00133F20, // 92
    0x00133F40, // 93
    0x00133F60, // 94
    0x00133F80, // 95
    0x0012C500, // 96
    0x0012F280, // 97
    0x00132960, // 98
    0x00000000, // 99
    0x00136D40, // 100
    0x0013B120, // 101
    0x0013D320, // 102
    0x0013D340, // 103
    0x0013D360, // 104
    0x0013D380, // 105
    0x00138FC0, // 106
    0x0013A460, // 107
    0x0013B440, // 108
    0x0013CAC0, // 109
    0x0013F8A0, // 110
    0x00140820, // 111
    0x001415C0, // 112
    0x001415E0, // 113
    0x00141600, // 114
    0x00141620, // 115
    0x0013E860, // 116
    0x00141340, // 117
    0x00142D60, // 118
    0x00146780, // 119
    0x00148260, // 120
    0x00149BA0, // 121
    0x0014B700, // 122
    0x0014B720, // 123
    0x0014E7A0, // 124
    0x0014E7C0, // 125
    0x002428C0, // 126
    0x00244F20, // 127
    0x00244F20, // 128
    0x00292C20, // 129
    0x00292C20, // 130
    0x0004DF80, // 131
    0x0004DF80, // 132
    0x0004DF80  // 133
};
```

---

## Ghidra (Critical)

- A **Ghidra MCP server is available**
- Project contains:
  - Base NSMB game
  - Partial symbol labeling
- This is the **authoritative source** for:
  - Function boundaries
  - Hook points
  - Branch safety
  - Calling conventions

**Never invent offsets.**
If an address is not confirmed in Ghidra, stop.

---

## Agent Workflow (Required)

For *every* code change:

1. Identify target function / behavior
2. Locate function in **Ghidra**
3. Confirm:
   - Address
   - ARM/THUMB mode
   - Call context
4. Implement the **smallest possible patch**
5. Build using NSMB-Docker
6. Test in melonDS
7. Report:
   - What changed
   - Where it hooks
   - Expected behavior
   - How to test

---

## Hard Rules (Do Not Violate)

- ❌ No ROMs committed
- ❌ No base-game assets committed
- ❌ No hex editing without tooling
- ❌ No guessing offsets
- ❌ No modifying `/data/` contents directly

- ✅ Hooks/jumps must reference Ghidra findings
- ✅ New files go in `nitrofs/z_new/`
- ✅ Code must build cleanly in Docker

---

## External References (Authoritative)

- NSMB-Docker  
  https://github.com/Ndymario/NSMB-Docker

- NCPatcher  
  https://github.com/TheGameratorT/NCPatcher

- NSMB Code Patching Reference  
  https://github.com/MammaMiaTeam/NSMB-Code-Reference

- NSMB Modding Wiki  
  https://bookstack.nsmbcentral.net/books/new-super-mario-bros


---

## ndspy — Using Python to Inspect / Modify NDS Files

Agents have access to **ndspy**, a Python library for reading, inspecting, and modifying
Nintendo DS ROMs and common DS file formats. This is useful for inspection, extraction,
and custom tooling outside of NSMB-Docker.

Official documentation:
https://ndspy.readthedocs.io/en/latest/

ndspy operates on ROM files directly and is best used for **analysis, extraction, and
automation**, not as a replacement for NSMB-Docker’s build pipeline.

---

### Installing ndspy

Use Python 3.6+ and install via pip:

```bash
pip install ndspy
```

---

### Example — Opening a ROM

Load an NSMB ROM file and inspect basic metadata:

```python
import ndspy.rom

rom = ndspy.rom.NintendoDSRom.fromFile("nsmb.nds")
print(f"ROM Name: {rom.name}, ID: {rom.idCode}")
```

Use this to verify you are working with the correct ROM before inspecting internals.

---

### Example — Listing Files in a ROM

Print all file paths contained in the ROM:

```python
for name in rom.filenames:
    print(name)
```

This helps locate NitroFS entries that may later be mirrored into `nitrofs/`.

---

### Example — Extracting a File by Name

Extract a specific file from the ROM:

```python
data = rom.getFileByName("level/0001/terrain.bin")

with open("terrain.bin", "wb") as f:
    f.write(data)
```

The extracted file can then be inspected, modified, or used as reference.

---

### Example — Working with NARC Archives

NARC archives are widely used in DS games to group assets.

```python
import ndspy.narc

narc = ndspy.narc.NARC.fromFile("example.narc")
print(f\"NARC contains {len(narc.files)} files\")

# Access a file inside the archive
file_data = narc.files[0]
```

Modified NARC archives can be re‑saved and later re‑inserted via tooling or `nitrofs/`.

---

### When to Use ndspy

Use ndspy for:
- Inspecting ROM contents and metadata
- Extracting files for analysis or reverse engineering
- Writing automation scripts for bulk operations
- Validating archive structure (`.narc`, `.sdat`, etc.)

Do **not** use ndspy to:
- Commit modified ROMs
- Replace the NSMB-Docker build process
- Perform manual binary patching

---

### Workflow Tips

- Use ndspy to explore and extract data, then mirror final changes into `nitrofs/`.
- Keep a clean ROM copy at all times; work only on duplicates.
- ndspy pairs well with custom Python scripts for batch extraction or validation.

---

-## GBATEK — Hardware Reference (NDS / DSi)

Agents have access to the **GBATEK documentation** locally:

```
/Users/ndymario/NSMB-DS-Modding/GBATEK NDS Docs.html
```

This is the authoritative low-level hardware reference for:
- Nintendo DS (NDS)
- Nintendo DSi (TWL)
- ARM9 / ARM7 memory maps
- Hardware registers
- Video, input, DMA, interrupts, timers
- DSi hardware extensions

When working on code mods that touch hardware behavior, rendering, input, memory,
interrupts, or timing, GBATEK must be consulted.

---

### When to Use GBATEK

Use GBATEK when:

- Working with memory-mapped IO registers (e.g. `0x04000000+`)
- Investigating video hardware (VRAM, BG layers, OBJ, palettes)
- Analyzing DMA usage or timers
- Understanding interrupt flags and IME/IE/IF behavior
- Studying ARM9 vs ARM7 shared memory
- Researching DSi-specific hardware behavior

Do not guess hardware register behavior.

If interacting with hardware-level features, verify:
- Register address
- Bit layout
- Required write ordering
- Timing constraints

---

### Commonly Useful Sections (NDS)

Agents should be familiar with these GBATEK sections:

- Memory Map (ARM9 / ARM7)
- I/O Register Map (0x04000000 region)
- VRAM Control Registers
- DISPSTAT / VCOUNT
- Interrupt Control (IME / IE / IF)
- DMA Channels
- Timers
- Key Input (KEYINPUT / KEYCNT)

When modifying rendering behavior or game timing,
cross-reference the hardware register definitions.

---

### DSi-Specific Notes

If targeting DSi (TWL mode):

- Confirm whether hardware behavior differs from NTR mode.
- Verify memory map extensions.
- Confirm additional WRAM or hardware blocks.
- Validate whether SCFG registers are relevant.

Never assume NTR behavior matches DSi behavior without verification.

---

### Hardware Safety Rules

- Never write to unknown registers.
- Never modify interrupt flags without understanding side effects.
- Do not alter DMA configuration unless fully understood.
- Always confirm bitfield layout before masking or shifting values.

If unsure, stop and consult GBATEK.

---

### Relationship to Ghidra

- Use **Ghidra** to identify where the game accesses hardware.
- Use **GBATEK** to understand what those registers actually do.
- Never infer hardware behavior purely from disassembly.

Ghidra tells you *what the game is doing*.
GBATEK tells you *what the hardware expects*.

Both must align before patching hardware behavior.

---

-## DSi Mode & Extended RAM (TWL Enhancements)

This project may target **DSi hardware enhancements** (TWL mode) including:
- 16MB main RAM (vs 4MB on NTR DS)
- Faster ARM9 clock (133MHz vs ~67MHz)
- Additional memory banking control

Running on DSi hardware alone does **NOT** automatically grant access to extra RAM.
The game must explicitly:
1. Detect DSi hardware at runtime
2. Configure extended memory mapping
3. Allocate and manage memory from the extended region

---

### Boot Requirements (Important)

To access full DSi hardware behavior:

- The game must be launched in **DSi mode (TWL mode)**.
- Standard DS cartridge boot will behave as NTR mode unless:
  - The title is DSi-enhanced, OR
  - A loader (e.g. nds-bootstrap / custom launcher) enables TWL mode.

Simply patching memory logic without ensuring DSi-mode boot will not expose extended RAM.

Agents must confirm the runtime environment before implementing DSi-specific logic.

---

### Runtime DSi Detection

Before using extended RAM, detect DSi hardware.

Detection methods may include:
- Checking SCFG registers
- Inspecting memory map behavior
- Using known hardware identification registers

Do NOT assume DSi presence.
All extended RAM logic must be gated behind a runtime check.

---

### DSi Extended RAM Overview

On DSi hardware:

- Main RAM expands to 16MB at `0x02000000`
- Memory banking is controlled via:
  - `SCFG_EXT`
  - `MBK1–MBK9`

These registers control how WRAM and extended memory are mapped
into the ARM9 address space.

Before using extended RAM:
- Confirm mapping registers
- Ensure memory region is visible
- Verify no overlap with existing heap or static allocations

Consult **GBATEK (DSi section)** for exact register definitions and bitfields.

---

### Using Extended RAM Safely

When enabling extended RAM usage:

1. Perform DSi detection at startup
2. Configure SCFG / MBK registers if required
3. Establish a separate allocator for extended RAM
4. Do NOT modify the original 4MB heap behavior unless intentional
5. Ensure fallback behavior exists for NTR hardware

Recommended strategy:
- Leave original game heap untouched
- Create a new memory pool in extended RAM
- Use it for:
  - Large buffers
  - Expanded object pools
  - Custom systems
  - Additional level data

---

### ARM9 vs ARM7 Considerations

- Some memory banks are shared or bank-switched between ARM9 and ARM7
- Confirm ownership before mapping
- Never assume exclusive access without verification

Consult GBATEK for:
- Shared WRAM behavior
- Bank switching implications

---

### Strict Rules for DSi Enhancements

- ❌ Never write to SCFG / MBK registers without verifying bitfields
- ❌ Never assume extended RAM is automatically usable
- ❌ Never break compatibility with NTR hardware unless explicitly required

- ✅ All DSi-specific logic must be runtime-checked
- ✅ All hardware behavior must be verified in GBATEK
- ✅ All address changes must be documented

---

------------------------------------------------------------------------

NITRO Developer Documentation (NitroSDK / Official APIs)

Agents have access to the official NITRO Developer Documentation
locally:

/Users/ndymario/NSMB-DS-Modding/NitroDevDocs

These PDF manuals document the original Nintendo DS SDK (NitroSDK),
including:

-   OS (threads, memory arenas, interrupts, alarms)
-   GX / 2D / 3D graphics APIs
-   File system (FS)
-   NARC / archive handling
-   Audio (SND / SDAT)
-   Input handling
-   System initialization
-   ARM9 ↔ ARM7 communication
-   Memory arena configuration
-   Cache behavior and DMA usage

This documentation reflects how Nintendo intended DS software to
interact with hardware and system services.

When reverse engineering or extending NSMB, these documents provide the
semantic meaning behind many SDK-style functions visible in Ghidra.

------------------------------------------------------------------------

When to Use Nitro Developer Documentation

Consult Nitro Developer Documentation when:

-   Identifying SDK functions in Ghidra
-   Investigating OS_* functions (threads, alarms, timers)
-   Understanding memory arena setup (OS_GetArenaLo/Hi)
-   Interpreting GX / G2 / G3 calls
-   Understanding heap behavior and allocation flow
-   Investigating archive loading logic
-   Researching VBlank / HBlank scheduling behavior
-   Modifying system initialization behavior

If a function appears to be part of the SDK, verify its purpose in Nitro
docs before modifying or hooking it.

------------------------------------------------------------------------

Relationship to Other References

Use references in this order:

1.  Ghidra → What NSMB is calling
2.  Nitro Developer Docs → What the SDK layer guarantees
3.  GBATEK → What the hardware actually does

Do not rely purely on symbol names or assumptions.
------------------------------------------------------------------------

## TODO (Future Expansion)
- Region-specific address handling
- Common hook templates
- Logging / debug strategies
- Overlay-specific patching rules
- Code style & naming conventions
