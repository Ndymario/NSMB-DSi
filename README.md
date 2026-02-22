# NSMB-DSi

NSMB code-mod project with:
- baseline NTR-safe behavior
- runtime-gated DSi enhancements
- melonDS direct-boot DSi bring-up path (no nds-bootstrap/TWiLight dependency)

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

## Test overlay payload

Generate a minimal payload (used for overlay runtime validation):

```bash
python3 ./tools/generate_test_overlay_payload.py --out ./build/mod_overlay_payload.bin --exports-offset 0x20
```

## melonDS run

Run with:
- Console Type: `DSi`
- Direct Boot: `ON`

Launch command:

```bash
/Users/ndymario/Programming/Cpp/melonDS/build/release-mac-x86_64/melonDS.app/Contents/MacOS/melonDS /Users/ndymario/NSMB-DS-Modding/NSMB-DSi/final.nds
```

Expected success logs:
- `[MELONDS-NSMB] direct-boot quirk active ...`
- `[MELONDS-NSMB] clamped MPU region1 to 16MB ...` (once)
- `[MODRUNTIME] TWL gate PASS ...`
- `[MODRUNTIME] Extra RAM pool ready ...`
- `[MODRUNTIME][OVERLAY] Overlay ready exports=...`
