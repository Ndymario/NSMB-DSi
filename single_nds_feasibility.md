# Single `.nds` Feasibility (No Patched melonDS)

## Question
Can this project ship as one `.nds` that enables DSi RAM mode without relying on a melonDS runtime patch?

## Evidence Summary

### 1) TWL card model requirements (documentation)
- TWL-enhanced/TWL cards include `header2`, `security2`, and `game2` regions; `game2` is TWL-readable only.
  - `Nintendo DS - DSi Enhanced - DSi Game Card Manual v2.02.pdf` PDF pages 9-13.
- TWL card ROM composition/constraints are not the same as legacy DS card assumptions.
  - `Nintendo DS - DSi Enhanced - DSi Game Card Manual v2.02.pdf` PDF pages 6-7.

### 2) Current runtime gate requirements (code)
- DSi mode only if all checks pass:
  - `SCFG_A9ROM == 0x0001`
  - `SCFG_EXT9.bit31 == 1`
  - `SCFG_EXT9[15:14] >= 2`
  - MPU region1 reflects `16MB`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:353`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:415`
- Current README expectations explicitly include patched melonDS quirk logs.
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/README.md:116`

### 3) Game-card boot constraints (NITRO doc)
- Bootable regions and secure/header regions are fixed; overlays and data binaries are app-loaded after boot.
  - `NITRO Programming Manual v1.62.pdf` PDF pages 53-54.

---

## Dependency on Patched Emulator Today

Current implementation is practically dependent on environment behavior that presents TWL-mode registers and a 16MB-compatible runtime setup. The code does not fully own MBK/SCFG lifecycle configuration and assumes a DSi-extra-RAM-ready execution context.

Result: without the patched launch behavior, this project commonly boots into NDS compatibility and disables DSi path.

---

## Go/No-Go Matrix

| Option | Definition | Feasibility | Notes |
|---|---|---|---|
| A | ROM-side changes only, same current mod model, no special TWL packaging flow | No | Current DS ROM patching flow does not produce a guaranteed true TWL boot context. |
| B | Single ROM with full TWL-compliant build/packaging + TWL boot/runtime constraints | Theoretical | Requires full TWL card-compatible packaging path and compatible runtime that honors it. |
| C | Not feasible for this project’s current DS ROM mod model | Yes (current reality) | Best practical verdict for this repository/toolchain today. |

## Verdict
**C (current model), with B as a future track only if toolchain and boot path are upgraded.**

---

## What B Would Require (Decision-Complete)

1. Build/packaging
- A TWL-aware ROM build pipeline producing valid TWL-enhanced layout (`header2/security2/game2` semantics).
- No dependence on patched emulator behavior to force SCFG/MPU state.

2. Runtime ownership
- Explicit MBK/SCFG lifecycle management and validation in code.
- Extended memory allocator integration (`OS_InitArenaEx` + MainEx arena APIs).

3. Loader conformance
- Unified file loader API with deterministic lifecycle and cache coherency boundaries.
- Overlay lifecycle aligned with FS overlay APIs or equivalently hardened custom path.

4. Compatibility envelope
- Define target environments:
  - emulator(s) with true TWL-mode behavior,
  - DS compatibility fallback path when TWL mode unavailable.

---

## Recommended Next Step
Treat “single `.nds` without patched melonDS” as a **separate track** from current stabilization:
1. Stabilize conformance and memory lifecycle first (P0/P1 from `DSI_RAM_REVIEW.md`).
2. Then prototype B with explicit acceptance criteria:
   - DSi gate passes without emulator patch logs.
   - No regressions in DS fallback path.

