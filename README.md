# NSMB-DSi

Some black magic to get NSMB to tap into the DSi's extra resources.

[!Warning]
> Currently, the patched ROM does not boot on actual hardware! You will need to use a patched version of melonDS to actually use this mod.

[!NOTE]
> This project was "vibe coded" lmao. If someone can make this work better, by all means please do! I was able to feed AI several resources to make the resulting code much better than just "hey, make DSi mode work".

## Runtime hooks

Ghidra-validated `main0` hooks:
- `ncp_hook(0x02005044, ModRuntime_BootstrapHook)` (`InitGame` callsite)
- `ncp_call(0x0204c93c, ModRuntime_SpawnDispatch)` (`Base::spawn` callsite)

Runtime files:
- `source/mod_runtime.hpp`
- `source/mod_runtime.cpp`
- `source/mod_overlay_api.hpp`

## Strict DSi gate

DSi mode is enabled only if all checks pass:
- `SCFG_A9ROM == 0x0001`
- `SCFG_EXT9.bit31 == 1`
- `SCFG_EXT9[15:14] >= 2`
- MPU region1 is `0x0200002F` (16MB main RAM mapping)

If any check fails, runtime stays in fallback mode and uses vanilla behavior.

## Memory layout

Overlay region:
- image: `0x02800000-0x028FFFFF`
- heap/work: `0x02900000-0x02BFFFFF`
- guard: `0x02C00000-0x02C0FFFF`

Extra RAM pool (separate from vanilla heap):
- base: `0x02C10000`
- size: `0x003F0000`

## Build

Standard wrapper:

```bash
./tools/build_with_overlay.sh
```

Build with explicit test overlay offsets:

```bash
MODOVL_ENTRY_OFFSET=0 MODOVL_EXPORTS_OFFSET=0x20 ./tools/build_with_overlay.sh
```

`final.nds` is the primary output.

## Patching melonDS

Reference implementation:

The current local patch set lives on `master` (working tree based on `8fa251f4`) and modifies these files:

- `src/DSi.cpp`
- `src/DSi.h`
- `src/CP15.cpp`
- `src/NDS.cpp`
- `src/NDSCart.cpp`

What those edits do:

1. `src/DSi.cpp`
- Detect NSMB by game code (`A2D*`).
- When running in DSi console mode with Direct Boot and NSMB, enable an `NsmbDirectBootQuirkActive` flag.
- In the DS-mode direct-boot path, keep the DS binary boot flow but expose DSi-style RAM state to the game:
  - force `SCFG_BIOS` low bits to `0x0001`
  - set `SCFG_EXT[0]` RAM limit field to `16MB`
  - call `ApplyNewRAMSize(2)`
- Emit:
  - `[MELONDS-NSMB] direct-boot quirk active for game code ...`

2. `src/DSi.h`
- Add a `NsmbDirectBootQuirkActive` member on `DSi`.
- Expose `IsNsmbDirectBootQuirkActive()` so CP15 can query whether the quirk should be applied.

3. `src/CP15.cpp`
- When NSMB direct-boot quirk mode is active, intercept ARM9 MPU region 1 writes (`0x610` / `0x611`).
- If the game tries to map `0x02000000` with a size smaller than 16MB, clamp the region size field to `16MB` (`0x0200002F`).
- Emit once per boot:
  - `[MELONDS-NSMB] clamped MPU region1 to 16MB for NSMB direct boot`

4. `src/NDS.cpp`
- Add the NO$GBA debug register window used by this project:
  - `0x04FFFA00-0x04FFFA0F`: Emulation ID string
  - `0x04FFFA10`: raw string out
  - `0x04FFFA14`: formatted string out
  - `0x04FFFA18`: formatted string out + newline
  - `0x04FFFA1C`: char out
- Route ARM7 reads/writes for that window even though it sits outside normal `0x04000000` IO space.
- This is what makes the project’s debug logging visible in melonDS.

5. `src/NDSCart.cpp`
- Accept a TWL payload image whose DSi region mask is zero if the DSi ARM9i/ARM7i payload fields are populated.
- Treat that case as region-free instead of forcing the ROM into bad-dump mode.

Exact code changes from the reference tree:

### `src/DSi.cpp`

```cpp
static bool IsNsmbTitle(const NDSHeader& header)
{
    // NSMB gamecodes across regions are A2D*
    return header.GameCode[0] == 'A' &&
           header.GameCode[1] == '2' &&
           header.GameCode[2] == 'D';
}
```

```cpp
void DSi::Reset()
{
    // ...
    NDS::Reset();
    NsmbDirectBootQuirkActive = false;

    // ...
}
```

```cpp
void DSi::SetupDirectBoot()
{
    // ...
    const bool isNsmbTitle = IsNsmbTitle(header);

    NsmbDirectBootQuirkActive = false;

    // ...
    if (dsmode)
    {
        SCFG_BIOS = 0x0303;
        if (isNsmbTitle)
        {
            NsmbDirectBootQuirkActive = true;
            Log(
                LogLevel::Info,
                "[MELONDS-NSMB] direct-boot quirk active for game code %.4s\n",
                header.GameCode);
        }

        // ...

        if (NsmbDirectBootQuirkActive)
        {
            // Keep DS binary path but expose DSi RAM settings to runtime checks.
            SCFG_BIOS = (SCFG_BIOS & ~0x0003) | 0x0001; // A9ROM low bits
            SCFG_EXT[0] &= ~0xC000;
            SCFG_EXT[0] |= (2 << 14); // RAM_LIMIT=16MB
            ApplyNewRAMSize(2);
        }
        else
        {
            SCFG_EXT[0] &= ~0xC000;
            ApplyNewRAMSize(0);
        }
    }
    // ...
}
```

### `src/DSi.h`

```cpp
public:
    bool GetFullBIOSBoot() const noexcept { return FullBIOSBoot; }
    void SetFullBIOSBoot(bool full) noexcept { FullBIOSBoot = full; }

    bool IsNsmbDirectBootQuirkActive() const noexcept { return NsmbDirectBootQuirkActive; }

    void SetDSPHLE(bool hle);
private:
    bool FullBIOSBoot;
    bool NsmbDirectBootQuirkActive = false;
    void Set_SCFG_Clock9(u16 val);
```

### `src/CP15.cpp`

```cpp
    case 0x610:
    case 0x611:
    case 0x620:
    case 0x621:
    case 0x630:
    case 0x631:
    case 0x640:
    case 0x641:
    case 0x650:
    case 0x651:
    case 0x660:
    case 0x661:
    case 0x670:
    case 0x671:
        if ((id == 0x610 || id == 0x611) && NDS.ConsoleType == 1)
        {
            auto* dsi = static_cast<DSi*>(&NDS);
            static bool nsmbMpuClampLogged = false;

            if (dsi->IsNsmbDirectBootQuirkActive())
            {
                const bool enabled = (val & 0x1) != 0;
                const u32 base = val & 0xFFFFF000;
                const u32 sizeField = (val >> 1) & 0x1F;

                if (enabled && base == 0x02000000 && sizeField < 0x17)
                {
                    val = (val & ~0x3E) | (0x17 << 1); // clamp to 16MB (0x0200002F)
                    if (!nsmbMpuClampLogged)
                    {
                        Log(
                            LogLevel::Info,
                            "[MELONDS-NSMB] clamped MPU region1 to 16MB for NSMB direct boot\n");
                        nsmbMpuClampLogged = true;
                    }
                }
            }
            else
                nsmbMpuClampLogged = false;
        }

        char log_output[1024];
        PU_Region[(id >> 4) & 0xF] = val;
```

### `src/NDS.cpp`

```cpp
u8 NDS::ARM7Read8(u32 addr)
{
    // ...

    // NO$GBA debug register window can be accessed outside 0x04000000 space.
    if (addr >= 0x04FFFA00 && addr < 0x04FFFA10)
        return NDS::ARM7IORead8(addr);

    // ...
}
```

```cpp
void NDS::ARM7Write32(u32 addr, u32 val)
{
    addr &= ~0x3;

    // NO$GBA debug register window can be written outside 0x04000000 space.
    if (addr >= 0x04FFFA10 && addr <= 0x04FFFA1C)
    {
        NDS::ARM7IOWrite32(addr, val);
        return;
    }

    // ...
}
```

```cpp
u8 NDS::ARM7IORead8(u32 addr)
{
    // ...

    // NO$GBA debug register "Emulation ID"
    if (addr >= 0x04FFFA00 && addr < 0x04FFFA10)
    {
        static char const emuID[16] = "melonDS " MELONDS_VERSION_BASE;
        auto idx = addr - 0x04FFFA00;
        return (u8)(emuID[idx]);
    }

    // ...
}
```

```cpp
void NDS::ARM7IOWrite32(u32 addr, u32 val)
{
    switch (addr)
    {
    // ...

    // NO$GBA debug register "String Out (raw)"
    case 0x04FFFA10:
        {
            char output[1024] = { 0 };
            char ch = '.';
            for (size_t i = 0; i < 1023 && ch != '\0'; i++)
            {
                ch = NDS::ARM7Read8(val + i);
                output[i] = ch;
            }
            Log(LogLevel::Debug, "%s", output);
            return;
        }

    // NO$GBA debug registers "String Out (with parameters)" and
    // "String Out (with parameters, plus linefeed)"
    case 0x04FFFA14:
    case 0x04FFFA18:
        {
            NocashPrint(1, val, 0x04FFFA18 == addr);
            return;
        }

    // NO$GBA debug register "Char Out"
    case 0x04FFFA1C:
        Log(LogLevel::Debug, "%c", val & 0xFF);
        return;
    }

    // ...
}
```

### `src/NDSCart.cpp`

```cpp
if (dsi && header.DSiRegionMask == RegionMask::NoRegion)
{
    const bool hasTwlPayload =
        header.DSiARM9iROMOffset != 0 && header.DSiARM9iSize != 0 &&
        header.DSiARM7iROMOffset != 0 && header.DSiARM7iSize != 0;
    if (hasTwlPayload)
    {
        Log(
            LogLevel::Warn,
            "DS header indicates DSi, but region is zero. Assuming region-free for TWL payload image.\n");
        header.DSiRegionMask = RegionMask::RegionFree;
        *(u32*)&cartrom[0x1B0] = (u32)RegionMask::RegionFree;
    }
    else
    {
        Log(LogLevel::Info, "DS header indicates DSi, but region is zero. Going in bad dump mode.\n");
        badDSiDump = true;
        dsi = false;
    }
}
```

To inspect the exact local patch from the reference tree:

```bash
git -C /Users/ndymario/Programming/Cpp/melonDS diff -- src/DSi.cpp src/DSi.h src/CP15.cpp src/NDS.cpp src/NDSCart.cpp
```

## Test overlay payload

Generate a minimal payload (used for overlay runtime validation):

```bash
python3 ./tools/generate_test_overlay_payload.py --out ./build/mod_overlay_payload.bin --exports-offset 0x20
```

## Ordinary overlay payload

`ordinary_overlay` is an adapted overlay module based on [An Ordinary NSMB Mod](https://github.com/Ndymario/Ordinary-NSMB-Mod)

Build + pack ordinary overlay and rebuild ROM:

```bash
./tools/build_with_ordinary_overlay.sh
```

This produces:
- `nitrofs/z_new/mod/ordinaryovl.bin`
- `nitrofs/z_new/mod/modovl.bin` (compat mirror for current runtime path)

Current translated custom IDs:
- `0x0300`: SpookyController entry (translated overlay-side controller trigger)
- `0x0301`: Spooky Chaser path

## Ordinary NitroFS sync

Ordinary NitroFS assets are copied into this repo under:
- `nitrofs/banner.bin`
- `nitrofs/course/H12_1.bin`
- `nitrofs/course/H12_1_bgdat.bin`
- `nitrofs/z_new/Ordinary/actors/*`
- `nitrofs/z_new/dummy.txt`

## Ordinary gameplay source port

To support spooky controller behavior, these Ordinary code modules are copied into this repo and built by NCPatcher:
- `source/ports/Ordinary/SpookyController.cpp`
- `source/ports/Ordinary/SpookyChaser.cpp`
- `source/NSBTX.cpp`
- `source/lighting/extralighting.cpp`
- `source/ports/Ordinary/util/collisionviewer.cpp`

## melonDS run

Run with:
- Console Type: `DSi`
- Direct Boot: `ON`

Expected success logs:
- `[MELONDS-NSMB] direct-boot quirk active ...`
- `[MELONDS-NSMB] clamped MPU region1 to 16MB ...` (once)
- `[MODRUNTIME] TWL gate PASS ...`
- `[MODRUNTIME] Extra RAM pool ready ...`
- `[MODRUNTIME][OVERLAY] Overlay ready exports=...`
