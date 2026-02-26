# DSi RAM Review Issue Handoff

This is the implementation handoff list mapped to concrete files/functions.

## P0 (Correctness / Crash)

### DSI-001: DSi pool lifecycle reset is not wired to scene transitions
- Severity: P0
- Symptoms: cross-area instability, stale resources, memory drift.
- Code:
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:771`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:777`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/ports/Ordinary/SpookyController.cpp:525`
- Required change:
  - Invoke centralized DSi memory lifecycle hooks on area enter/leave and overlay unload.

### DSI-002: Per-module OOM fallback hardcoded to real file ID 1529
- Severity: P0
- Symptoms: Bowser path special-cased; non-Bowser failures remain unmanaged.
- Code:
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/ports/Ordinary/MemoryDebug.cpp:15`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/ports/Ordinary/MemoryDebug.cpp:96`
- Required change:
  - Replace with policy-driven fallback in shared loader.

### DSI-003: Cache coherence boundary lacks explicit write-buffer drain
- Severity: P0
- Symptoms: intermittent texture/model corruption after transitions.
- Code:
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:589`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:735`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/ports/Ordinary/MemoryDebug.cpp:121`
- Required change:
  - Add required `DC_WaitWriteBufferEmpty` boundaries for cross-bus consumers.

### DSI-004: Duplicate loader ownership between runtime and `MemoryDebug`
- Severity: P0
- Symptoms: diverging behavior, hard-to-reason state, fragile fixes.
- Code:
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:689`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/ports/Ordinary/MemoryDebug.cpp:83`
- Required change:
  - Route all load/promote/fallback logic through one shared `DsiMem` API.

## P1 (Data Integrity / Graphics)

### DSI-005: Persistent static resource pointers have no explicit invalidation policy
- Severity: P1
- Symptoms: occasional incorrect Ordinary graphics after area change.
- Code:
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/ports/Ordinary/SpookyChaser.cpp:12`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/ports/Ordinary/SpookyBoss/SpookyBoss.cpp:17`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/ports/Ordinary/SpookyController.cpp:45`
- Required change:
  - Tie asset pointer validity to `DsiMem` lifecycle generation IDs.

### DSI-006: Custom overlay loader duplicates SDK overlay responsibilities
- Severity: P1
- Symptoms: extra correctness burden for clear/init/cache/start sequence.
- Code:
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:466`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:569`
- Required change:
  - Either migrate to FS overlay APIs or fully document parity requirements/tests.

### DSI-007: SCFG/MBK mapping ownership is assumed, not managed
- Severity: P1
- Symptoms: runtime portability depends on emulator/boot state.
- Code:
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:10`
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:433`
- Required change:
  - Add explicit mapping ownership checks/config or hard-fail diagnostics.

## P2 (Maintainability)

### DSI-008: Extended memory allocator bypasses SDK arena model
- Severity: P2
- Symptoms: custom allocator has no integration with standard arena lifecycle.
- Code:
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/source/mod_runtime.cpp:649`
- Required change:
  - Introduce `OS_InitArenaEx`/MainEx arena-backed policy path.

### DSI-009: AGENTS docs path typo for NitroDevDocs
- Severity: P2
- Symptoms: tooling/instruction mismatch.
- Code:
  - `/Users/ndymario/NSMB-DS-Modding/NSMB-DSi/AGENTS.md:838`
- Required change:
  - Fix `NitroDevDocks` -> `NitroDevDocs`.

---

## Proposed Implementation Order
1. DSI-004 (unified loader API), DSI-001 (lifecycle wiring), DSI-003 (cache/write-buffer boundaries).
2. DSI-002 (remove file-id special-case), DSI-005 (resource generation validity).
3. DSI-006 (overlay loader normalization), DSI-007 (mapping ownership).
4. DSI-008 and DSI-009 cleanup.

