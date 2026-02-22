#include <cstddef>
#include <cstdint>

#include "nsmb.hpp"

#include "mod_runtime.hpp"

namespace {

constexpr uintptr_t kScfgA9RomAddr = 0x04004000;
constexpr uintptr_t kScfgExt9Addr = 0x04004008;
constexpr uintptr_t kSndExCntAddr = 0x04004700;

constexpr uintptr_t kBaseSpawnAddr = 0x0204c9a4;
constexpr uintptr_t kFsConvertPathToFileIdAddr = 0x0206a54c;
constexpr uintptr_t kFsInitFileAddr = 0x0206a778;
constexpr uintptr_t kFsOpenFileFastAddr = 0x0206a478;
constexpr uintptr_t kFsReadFileAddr = 0x0206a2f0;
constexpr uintptr_t kFsCloseFileAddr = 0x0206a3e0;
constexpr uintptr_t kDcFlushRangeAddr = 0x02065ce8;
constexpr uintptr_t kIcInvalidateRangeAddr = 0x02065d24;
constexpr uintptr_t kOsEnableProtectionUnitAddr = 0x02066230;
constexpr uintptr_t kOsSetProtectionRegion1Addr = 0x02066250;

constexpr uintptr_t kOverlayImageBaseAddr = 0x02800000;
constexpr uintptr_t kOverlayImageEndAddr = 0x028fffff;
constexpr uintptr_t kOverlayHeapBaseAddr = 0x02900000;
constexpr uintptr_t kOverlayGuardBaseAddr = 0x02c00000;
constexpr uintptr_t kExtraRamPoolStrictBaseAddr = 0x02c10000;
constexpr uintptr_t kExtraRamPoolStrictEndAddr = 0x02ffffff;
constexpr uint32_t kExtraRamPoolStrictSize = kExtraRamPoolStrictEndAddr - kExtraRamPoolStrictBaseAddr + 1;
constexpr uint32_t kExtraRamProbePattern = 0x44534921;  // "DSI!"
constexpr uint16_t kScfgA9RomDsiMode = 0x0001;

// Ghidra: OS_InitArenaEx writes 0x0200002B to region1 (4MB). 16MB is size-field +2.
constexpr uint32_t kMpuMainRamRegion4Mb = 0x0200002b;
constexpr uint32_t kMpuMainRamRegion16Mb = 0x0200002f;
static_assert(kMpuMainRamRegion16Mb == (kMpuMainRamRegion4Mb + 4u),
              "MPU main RAM region16 descriptor should be the 4MB descriptor with a larger size.");

static_assert(kExtraRamPoolStrictBaseAddr > kOverlayGuardBaseAddr,
              "DSi extra RAM pool must be above overlay guard.");

constexpr uint32_t kOverlayMaxImageSize = kOverlayImageEndAddr - kOverlayImageBaseAddr + 1;
constexpr uint32_t kOverlayTotalRegionSize = kOverlayGuardBaseAddr - kOverlayImageBaseAddr;
constexpr uint32_t kOverlayMaxBssSize = kOverlayTotalRegionSize - kOverlayMaxImageSize;

constexpr uint16_t kCustomObjectMin = 0x0300;
constexpr uint16_t kCustomObjectMax = 0x03ff;
constexpr char kOverlayPath[] = "/z_new/mod/modovl.bin";

struct FSFileID {
    void *archive;
    uint32_t file_id;
};

// Size derived from stack usage in readFileIntoBufferWithMax (FStack_68).
struct FSFile {
    uint8_t raw[0x68];
};

using BaseSpawnFn = void *(*)(uint16_t object_id, void *parent_node, uint32_t settings,
                              uint8_t base_type);
using FSConvertPathToFileIDFn = int (*)(FSFileID *id, char *path);
using FSInitFileFn = void (*)(FSFile *file);
using FSOpenFileFastFn = int (*)(FSFile *file, int archive, uint32_t file_id);
using FSReadFileFn = int (*)(FSFile *file, void *dest, uint32_t size);
using FSCloseFileFn = int (*)(FSFile *file);
using DCFlushRangeFn = void (*)(void *start, int size);
using ICInvalidateRangeFn = void (*)(void *start, int size);
using OSEnableProtectionUnitFn = uint32_t (*)();
using OSSetProtectionRegion1Fn = void (*)(uint32_t);

BaseSpawnFn BaseSpawnOriginal = reinterpret_cast<BaseSpawnFn>(kBaseSpawnAddr);
FSConvertPathToFileIDFn FSConvertPathToFileID =
    reinterpret_cast<FSConvertPathToFileIDFn>(kFsConvertPathToFileIdAddr);
FSInitFileFn FSInitFile = reinterpret_cast<FSInitFileFn>(kFsInitFileAddr);
FSOpenFileFastFn FSOpenFileFast = reinterpret_cast<FSOpenFileFastFn>(kFsOpenFileFastAddr);
FSReadFileFn FSReadFile = reinterpret_cast<FSReadFileFn>(kFsReadFileAddr);
FSCloseFileFn FSCloseFile = reinterpret_cast<FSCloseFileFn>(kFsCloseFileAddr);
DCFlushRangeFn DCFlushRange = reinterpret_cast<DCFlushRangeFn>(kDcFlushRangeAddr);
ICInvalidateRangeFn ICInvalidateRange =
    reinterpret_cast<ICInvalidateRangeFn>(kIcInvalidateRangeAddr);
OSEnableProtectionUnitFn OSEnableProtectionUnit =
    reinterpret_cast<OSEnableProtectionUnitFn>(kOsEnableProtectionUnitAddr);
OSSetProtectionRegion1Fn OSSetProtectionRegion1 =
    reinterpret_cast<OSSetProtectionRegion1Fn>(kOsSetProtectionRegion1Addr);

auto *const kOverlayImageBase = reinterpret_cast<uint8_t *>(kOverlayImageBaseAddr);
ModRuntimeMode g_mod_runtime_mode = ModRuntimeMode::UNINITIALIZED;
ModRuntimeDebugState g_debug_state = {};
bool g_bootstrap_finished = false;
bool g_spawn_in_progress = false;
bool g_logged_detect_result = false;
bool g_extra_ram_pool_ready = false;
uint32_t g_extra_ram_pool_cursor = 0;
uintptr_t g_extra_ram_pool_base_addr = kExtraRamPoolStrictBaseAddr;
uint32_t g_extra_ram_pool_size = kExtraRamPoolStrictSize;
ModOverlayExports *g_overlay_exports = nullptr;
ModOverlayHostApi g_host_api = {};
constexpr uint32_t kDetectOk = 0;
constexpr uint32_t kDetectFailScfgExt9Bit31 = 1;
constexpr uint32_t kDetectFailScfgA9Rom = 2;
constexpr uint32_t kDetectFailRamLimit = 3;
constexpr uint32_t kDetectFailMpuConfig = 5;

constexpr uint32_t kTwlFailStageNone = 0;
constexpr uint32_t kTwlFailStageExt9Bit31 = 1;
constexpr uint32_t kTwlFailStageA9Rom = 2;
constexpr uint32_t kTwlFailStageRamLimit = 3;
constexpr uint32_t kTwlFailStageMpu = 4;

constexpr uint32_t kOverlaySkipNone = 0;
constexpr uint32_t kOverlaySkipNotTwl = 1;
constexpr uint32_t kOverlaySkipFileUnavailable = 2;
constexpr uint32_t kOverlaySkipHeaderInvalid = 3;
constexpr uint32_t kOverlaySkipEntryFailed = 4;
constexpr uint32_t kOverlaySkipAbiMismatch = 5;

struct DsiRegisterState {
    uint32_t a9rom;
    uint32_t ext9;
    uint32_t ext9_bit31;
    uint32_t ram_limit;
    uint32_t sndexcnt;
    bool dsi_console_hint;
};

const char *DetectReasonLabelInternal(uint32_t code) {
    switch (code) {
        case kDetectOk:
            return "ok";
        case kDetectFailScfgExt9Bit31:
            return "ext9_bit31_off";
        case kDetectFailScfgA9Rom:
            return "a9rom_not_twl";
        case kDetectFailRamLimit:
            return "ram_limit_below_16mb";
        case kDetectFailMpuConfig:
            return "mpu_region1_not_16mb";
        default:
            return "unknown";
    }
}

const char *OverlaySkipReasonLabelInternal(uint32_t code) {
    switch (code) {
        case kOverlaySkipNone:
            return "none";
        case kOverlaySkipNotTwl:
            return "not_twl_mode";
        case kOverlaySkipFileUnavailable:
            return "overlay_file_unavailable";
        case kOverlaySkipHeaderInvalid:
            return "overlay_header_invalid";
        case kOverlaySkipEntryFailed:
            return "overlay_entry_failed";
        case kOverlaySkipAbiMismatch:
            return "overlay_abi_mismatch";
        default:
            return "unknown";
    }
}

inline uint16_t ReadU16(uintptr_t addr) {
    return *reinterpret_cast<volatile uint16_t *>(addr);
}

inline uint32_t ReadU32(uintptr_t addr) {
    return *reinterpret_cast<volatile uint32_t *>(addr);
}

uint32_t ReadMpuMainRamRegionValue() {
    uint32_t value = 0;
    asm volatile("mrc p15, 0, %0, c6, c1, 0" : "=r"(value));
    return value;
}

void FillBytes(uint8_t *dest, uint8_t value, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) {
        dest[i] = value;
    }
}

void ZeroBytes(uint8_t *dest, uint32_t size) {
    FillBytes(dest, 0, size);
}

uint32_t ComputeCrc32(const void *data, uint32_t size) {
    uint32_t crc = 0xffffffffu;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const uint32_t mask = alignment - 1u;
    return (value + mask) & ~mask;
}

void ResetExtraRamPoolState() {
    g_extra_ram_pool_ready = false;
    g_extra_ram_pool_cursor = 0;
    g_debug_state.extram_pool_base = static_cast<uint32_t>(g_extra_ram_pool_base_addr);
    g_debug_state.extram_pool_size = g_extra_ram_pool_size;
    g_debug_state.extram_pool_cursor = 0;
}

bool InitializeExtraRamPool() {
    ResetExtraRamPoolState();
    g_debug_state.extram_probe_attempts++;
    g_debug_state.extram_probe_addr = static_cast<uint32_t>(g_extra_ram_pool_base_addr);
    g_debug_state.extram_probe_pattern = kExtraRamProbePattern;

    volatile uint32_t *const probe = reinterpret_cast<volatile uint32_t *>(g_extra_ram_pool_base_addr);
    const uint32_t saved = *probe;
    *probe = kExtraRamProbePattern;
    const uint32_t readback = *probe;
    *probe = saved;

    g_debug_state.extram_probe_readback = readback;
    if (readback != kExtraRamProbePattern) {
        g_debug_state.extram_probe_failures++;
        return false;
    }

    g_debug_state.extram_probe_successes++;
    g_extra_ram_pool_ready = true;
    return true;
}

DsiRegisterState ReadDsiRegisters() {
    DsiRegisterState state = {};
    state.a9rom = ReadU16(kScfgA9RomAddr);
    state.ext9 = ReadU32(kScfgExt9Addr);
    state.ext9_bit31 = (state.ext9 >> 31) & 1u;
    state.ram_limit = (state.ext9 >> 14) & 0x3u;
    state.sndexcnt = ReadU32(kSndExCntAddr);
    state.dsi_console_hint = (state.sndexcnt & 0xA000u) == 0x8000u;
    return state;
}

bool IsStrictTwlReady(const DsiRegisterState &state) {
    return state.ext9_bit31 != 0u && state.a9rom == kScfgA9RomDsiMode && state.ram_limit >= 2u;
}

bool IsDsiHwLikely(const DsiRegisterState &state) {
    uint32_t hints = 0;
    if (state.dsi_console_hint) {
        hints++;
    }
    if ((state.ext9 & 0x03000000u) == 0x03000000u) {
        hints++;
    }
    if (state.a9rom == 0x0001u || state.a9rom == 0x0003u) {
        hints++;
    }
    return hints >= 2u;
}

void WriteDetectSnapshot(const DsiRegisterState &state, bool extra_ram_ready) {
    g_debug_state.raw_scfg_a9rom = state.a9rom;
    g_debug_state.raw_scfg_ext9 = state.ext9;
    g_debug_state.raw_scfg_ext9_ram_limit = state.ram_limit;
    g_debug_state.raw_sndexcnt = state.sndexcnt;
    g_debug_state.dsi_console_hint = state.dsi_console_hint ? 1u : 0u;
    g_debug_state.dsi_hw_likely = IsDsiHwLikely(state) ? 1u : 0u;
    g_debug_state.dsi_compat_mode_likely =
        (g_debug_state.dsi_hw_likely != 0u && !extra_ram_ready) ? 1u : 0u;
}

void LogTwlGateSnapshot(const char *result_tag) {
    const uint32_t ext9_bit31 = (g_debug_state.raw_scfg_ext9 >> 31) & 1u;
    Log() << "[MODRUNTIME] TWL gate " << result_tag
          << " A9ROM=" << Log::Hex << g_debug_state.raw_scfg_a9rom
          << " EXT9=" << g_debug_state.raw_scfg_ext9 << Log::Dec
          << " EXT9_BIT31=" << ext9_bit31
          << " RAM_LIMIT=" << g_debug_state.raw_scfg_ext9_ram_limit
          << " MPU_BEFORE=" << Log::Hex << g_debug_state.mpu_mainram_before
          << " MPU_AFTER=" << g_debug_state.mpu_mainram_after << Log::Dec
          << " FAIL_STAGE=" << g_debug_state.twl_fail_stage
          << " REASON=" << g_debug_state.detect_fail_reason
          << " REASON_LABEL=" << DetectReasonLabelInternal(g_debug_state.detect_fail_reason)
          << "\n";
}

bool ConfigureMpuMainRamRegion16Mb() {
    g_debug_state.mpu_update_attempts++;
    g_debug_state.mpu_mainram_before = ReadMpuMainRamRegionValue();
    g_debug_state.mpu_mainram_expected = kMpuMainRamRegion16Mb;

    OSSetProtectionRegion1(kMpuMainRamRegion16Mb);
    OSEnableProtectionUnit();

    g_debug_state.mpu_mainram_after = ReadMpuMainRamRegionValue();
    if (g_debug_state.mpu_mainram_after == kMpuMainRamRegion16Mb) {
        g_debug_state.mpu_update_successes++;
        return true;
    }

    g_debug_state.mpu_update_failures++;
    return false;
}

bool DetectAndEnableDsiExtraRam() {
    DsiRegisterState state = ReadDsiRegisters();
    g_debug_state.dsi_compat_warning_emitted = 0u;
    g_debug_state.twl_fail_stage = kTwlFailStageNone;
    g_debug_state.compat_unlock_candidate = 0u;
    g_debug_state.compat_unlock_active = 0u;
    g_debug_state.compat_unlock_attempts = 0u;
    g_debug_state.compat_unlock_successes = 0u;
    g_debug_state.compat_unlock_failures = 0u;
    g_debug_state.compat_preserve_attempts = 0u;
    g_debug_state.compat_preserve_successes = 0u;
    g_debug_state.compat_preserve_failures = 0u;
    g_debug_state.raw_scfg_a9rom_before_unlock = state.a9rom;
    g_debug_state.raw_scfg_ext9_before_unlock = state.ext9;
    g_debug_state.raw_scfg_a9rom_after_unlock = state.a9rom;
    g_debug_state.raw_scfg_ext9_after_unlock = state.ext9;
    g_debug_state.raw_scfg_ext9_ram_limit_after_unlock = state.ram_limit;

    g_extra_ram_pool_base_addr = kExtraRamPoolStrictBaseAddr;
    g_extra_ram_pool_size = kExtraRamPoolStrictSize;

    bool extra_ram_ready = false;
    if (IsStrictTwlReady(state)) {
        extra_ram_ready = ConfigureMpuMainRamRegion16Mb();
    }

    WriteDetectSnapshot(state, extra_ram_ready);
    if (extra_ram_ready) {
        g_debug_state.detect_fail_reason = kDetectOk;
        g_debug_state.strict_twl_passed++;
        return true;
    }

    g_debug_state.strict_twl_failed++;
    if (g_debug_state.mpu_update_failures != 0u) {
        g_debug_state.twl_fail_stage = kTwlFailStageMpu;
        g_debug_state.detect_fail_reason = kDetectFailMpuConfig;
    } else if (state.ext9_bit31 == 0u) {
        g_debug_state.twl_fail_stage = kTwlFailStageExt9Bit31;
        g_debug_state.detect_fail_reason = kDetectFailScfgExt9Bit31;
    } else if (state.ram_limit < 2u) {
        g_debug_state.twl_fail_stage = kTwlFailStageRamLimit;
        g_debug_state.detect_fail_reason = kDetectFailRamLimit;
    } else {
        g_debug_state.twl_fail_stage = kTwlFailStageA9Rom;
        g_debug_state.detect_fail_reason = kDetectFailScfgA9Rom;
    }

    return false;
}

bool LoadOverlayImage(uint32_t *out_bytes_read) {
    FSFileID file_id = {};
    if (FSConvertPathToFileID(&file_id, const_cast<char *>(kOverlayPath)) == 0) {
        return false;
    }

    FSFile file = {};
    FSInitFile(&file);
    const int archive_handle = static_cast<int>(reinterpret_cast<uintptr_t>(file_id.archive));
    if (FSOpenFileFast(&file, archive_handle, file_id.file_id) == 0) {
        return false;
    }

    FillBytes(kOverlayImageBase, 0xa5, kOverlayMaxImageSize);
    const int bytes_read = FSReadFile(&file, kOverlayImageBase, kOverlayMaxImageSize);
    FSCloseFile(&file);

    if (bytes_read <= 0) {
        return false;
    }

    *out_bytes_read = static_cast<uint32_t>(bytes_read);
    return true;
}

bool ValidateOverlayHeader(const ModOverlayHeader *header, uint32_t bytes_read) {
    if (header->magic != kModOverlayMagic) {
        return false;
    }
    if (header->abi_version != kModOverlayAbiVersion) {
        return false;
    }
    if (header->header_size < sizeof(ModOverlayHeader)) {
        return false;
    }
    if (header->image_size < header->header_size || header->image_size > kOverlayMaxImageSize) {
        return false;
    }
    if (header->image_size > bytes_read) {
        return false;
    }
    if (header->entry_offset >= header->image_size) {
        return false;
    }
    if (header->exports_offset >= header->image_size) {
        return false;
    }
    if (header->bss_size > kOverlayMaxBssSize) {
        return false;
    }
    const uint32_t total_size = header->image_size + header->bss_size;
    if (total_size < header->image_size || total_size > kOverlayTotalRegionSize) {
        return false;
    }

    const uint32_t header_crc =
        ComputeCrc32(header, static_cast<uint32_t>(offsetof(ModOverlayHeader, header_crc32)));
    if (header_crc != header->header_crc32) {
        return false;
    }

    const uint32_t payload_size = header->image_size - header->header_size;
    const uint32_t image_crc =
        ComputeCrc32(kOverlayImageBase + header->header_size, payload_size);
    if (image_crc != header->image_crc32) {
        return false;
    }

    return true;
}

uint32_t Host_GetRuntimeMode() {
    return static_cast<uint32_t>(g_mod_runtime_mode);
}

uint32_t Host_IsDsiModeEnabled() {
    return ModRuntime_IsDsiModeEnabled() ? 1u : 0u;
}

void *Host_SpawnOriginal(uint16_t object_id, void *parent_node, uint32_t settings,
                         uint8_t base_type) {
    return BaseSpawnOriginal(object_id, parent_node, settings, base_type);
}

void SetupHostApi() {
    g_host_api.abi_version = kModOverlayAbiVersion;
    g_host_api.host_flags = 0;
    g_host_api.crc32 = ComputeCrc32;
    g_host_api.spawn_original = Host_SpawnOriginal;
    g_host_api.get_runtime_mode = Host_GetRuntimeMode;
    g_host_api.is_dsi_mode_enabled = Host_IsDsiModeEnabled;
    g_host_api.reserved[0] = kOverlayHeapBaseAddr;
    g_host_api.reserved[1] = kOverlayGuardBaseAddr - kOverlayHeapBaseAddr;
    g_host_api.reserved[2] = 0;
}

bool TryLoadAndStartOverlay() {
    g_debug_state.overlay_skip_reason = kOverlaySkipNone;
    g_debug_state.overlay_load_attempts++;
    uint32_t bytes_read = 0;
    if (!LoadOverlayImage(&bytes_read)) {
        g_debug_state.overlay_skip_reason = kOverlaySkipFileUnavailable;
        g_debug_state.overlay_load_failures++;
        return false;
    }

    const auto *header = reinterpret_cast<const ModOverlayHeader *>(kOverlayImageBase);
    if (!ValidateOverlayHeader(header, bytes_read)) {
        g_debug_state.overlay_skip_reason = kOverlaySkipHeaderInvalid;
        g_debug_state.overlay_load_failures++;
        return false;
    }

    ZeroBytes(kOverlayImageBase + header->image_size, header->bss_size);

    const uint32_t total_size = header->image_size + header->bss_size;
    DCFlushRange(kOverlayImageBase, static_cast<int>(total_size));
    ICInvalidateRange(kOverlayImageBase, static_cast<int>(header->image_size));

    uintptr_t entry_addr = kOverlayImageBaseAddr + (header->entry_offset & ~1u);
    entry_addr |= (header->entry_offset & 1u);
    auto entry_fn = reinterpret_cast<ModOverlayEntryFn>(entry_addr);

    g_overlay_exports = reinterpret_cast<ModOverlayExports *>(kOverlayImageBase + header->exports_offset);
    if (!entry_fn(&g_host_api)) {
        g_debug_state.overlay_skip_reason = kOverlaySkipEntryFailed;
        g_overlay_exports = nullptr;
        g_debug_state.overlay_load_failures++;
        return false;
    }
    if (g_overlay_exports->abi_version != kModOverlayAbiVersion) {
        g_debug_state.overlay_skip_reason = kOverlaySkipAbiMismatch;
        g_overlay_exports = nullptr;
        g_debug_state.overlay_load_failures++;
        return false;
    }

    g_debug_state.overlay_load_successes++;
    return true;
}

}  // namespace

bool ModRuntime_IsDsiModeEnabled() {
    return g_mod_runtime_mode == ModRuntimeMode::DSI_MODE_NO_OVERLAY ||
           g_mod_runtime_mode == ModRuntimeMode::DSI_MODE_OVERLAY_READY;
}

bool ModRuntime_IsOverlayReady() {
    return g_mod_runtime_mode == ModRuntimeMode::DSI_MODE_OVERLAY_READY;
}

bool ModRuntime_IsDsiCompatModeLikely() {
    return !ModRuntime_IsDsiModeEnabled() && g_debug_state.dsi_compat_mode_likely != 0u;
}

bool ModRuntime_IsCompatUnlockEnabled() {
    return false;
}

bool ModRuntime_IsExtraRamPoolReady() {
    return g_extra_ram_pool_ready;
}

void *ModRuntime_ExtraRamAlloc(uint32_t size, uint32_t alignment) {
    g_debug_state.extram_alloc_calls++;
    g_debug_state.extram_last_alloc_size = size;
    g_debug_state.extram_last_alloc_alignment = alignment;
    g_debug_state.extram_last_alloc_addr = 0;

    if (!g_extra_ram_pool_ready || size == 0) {
        g_debug_state.extram_alloc_failures++;
        return nullptr;
    }

    if (alignment < 4u) {
        alignment = 4u;
    }
    if ((alignment & (alignment - 1u)) != 0u) {
        alignment = 4u;
    }

    const uint32_t aligned_cursor = AlignUp(g_extra_ram_pool_cursor, alignment);
    if (aligned_cursor > g_extra_ram_pool_size || size > (g_extra_ram_pool_size - aligned_cursor)) {
        g_debug_state.extram_alloc_failures++;
        return nullptr;
    }

    uint8_t *const base = reinterpret_cast<uint8_t *>(g_extra_ram_pool_base_addr);
    void *ptr = base + aligned_cursor;
    g_extra_ram_pool_cursor = aligned_cursor + size;
    const bool first_success = (g_debug_state.extram_alloc_successes == 0u);

    g_debug_state.extram_pool_cursor = g_extra_ram_pool_cursor;
    g_debug_state.extram_last_alloc_addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ptr));
    g_debug_state.extram_alloc_successes++;
    if (first_success) {
        Log() << "[MODRUNTIME] Extra RAM pool first alloc base=" << Log::Hex
              << g_extra_ram_pool_base_addr << " size=" << g_extra_ram_pool_size
              << " addr=" << g_debug_state.extram_last_alloc_addr << Log::Dec << "\n";
    }
    return ptr;
}

void ModRuntime_ExtraRamReset() {
    g_extra_ram_pool_cursor = 0;
    g_debug_state.extram_pool_cursor = 0;
}

ModRuntimeMode ModRuntime_GetMode() {
    return g_mod_runtime_mode;
}

const ModRuntimeDebugState *ModRuntime_GetDebugState() {
    return &g_debug_state;
}

const char *ModRuntime_GetDetectReasonLabel(uint32_t code) {
    return DetectReasonLabelInternal(code);
}

const char *ModRuntime_GetOverlaySkipReasonLabel(uint32_t code) {
    return OverlaySkipReasonLabelInternal(code);
}

bool ModRuntime_TryLateCompatEnable() {
    Log() << "[MODRUNTIME][COMPAT][WARNING] Late compat unlock is unsupported in strict TWL mode.\n";
    return false;
}

extern "C" ncp_hook(0x02005044) void ModRuntime_BootstrapHook() {
    g_debug_state.bootstrap_calls++;
    g_debug_state.bootstrap_seen++;
    g_debug_state.last_mode_snapshot = g_mod_runtime_mode;

    if (g_bootstrap_finished) {
        return;
    }
    g_bootstrap_finished = true;
    Log() << "[MODRUNTIME] Bootstrap hook reached (pre-detect)\n";

    if (!DetectAndEnableDsiExtraRam()) {
        ResetExtraRamPoolState();
        g_debug_state.overlay_skip_reason = kOverlaySkipNotTwl;
        g_mod_runtime_mode = ModRuntimeMode::NDS_OR_DSI_DISABLED;
        g_debug_state.nds_fallback_boots++;
        g_debug_state.last_mode_snapshot = g_mod_runtime_mode;

        if (!g_logged_detect_result) {
            g_logged_detect_result = true;
            LogTwlGateSnapshot("FAIL");
            Log() << "[MODRUNTIME] DSi detect FAILED reason=" << g_debug_state.detect_fail_reason
                  << " (" << DetectReasonLabelInternal(g_debug_state.detect_fail_reason) << ")"
                  << " A9ROM=" << Log::Hex << g_debug_state.raw_scfg_a9rom
                  << " EXT9=" << g_debug_state.raw_scfg_ext9 << Log::Dec
                  << " RAM_LIMIT=" << g_debug_state.raw_scfg_ext9_ram_limit
                  << " SNDEXCNT=" << Log::Hex << g_debug_state.raw_sndexcnt << Log::Dec
                  << " DSI_HINT=" << g_debug_state.dsi_console_hint
                  << " DSI_COMPAT_LIKELY=" << g_debug_state.dsi_compat_mode_likely
                  << " MPU_EXPECTED=" << Log::Hex << g_debug_state.mpu_mainram_expected
                  << " MPU_AFTER=" << g_debug_state.mpu_mainram_after << Log::Dec
                  << "\n";
            if (g_debug_state.dsi_console_hint != 0) {
                Log() << "[MODRUNTIME] DSi console hint is set, but runtime is in DS compatibility mode.\n";
            }
            if (ModRuntime_IsDsiCompatModeLikely()) {
                g_debug_state.dsi_compat_warning_emitted = 1u;
                Log() << "[MODRUNTIME][WARNING] DSi hardware is likely detected, but ARM9 is running in NDS "
                         "compatibility mode. DSi RAM features are unavailable in the current boot mode.\n";
            }
        }
        return;
    }

    if (!g_logged_detect_result) {
        g_logged_detect_result = true;
        LogTwlGateSnapshot("PASS");
        Log() << "[MODRUNTIME] DSi detect OK"
              << " A9ROM=" << Log::Hex << g_debug_state.raw_scfg_a9rom
              << " EXT9=" << g_debug_state.raw_scfg_ext9 << Log::Dec
              << " RAM_LIMIT=" << g_debug_state.raw_scfg_ext9_ram_limit
              << " SNDEXCNT=" << Log::Hex << g_debug_state.raw_sndexcnt << Log::Dec
              << " DSI_HINT=" << g_debug_state.dsi_console_hint
              << " DSI_COMPAT_LIKELY=" << g_debug_state.dsi_compat_mode_likely
              << " MPU_R1=" << Log::Hex << g_debug_state.mpu_mainram_after << Log::Dec
              << "\n";
    }

    g_debug_state.dsi_detected_boots++;
    g_debug_state.overlay_skip_reason = kOverlaySkipNone;
    g_mod_runtime_mode = ModRuntimeMode::DSI_MODE_NO_OVERLAY;
    if (InitializeExtraRamPool()) {
        Log() << "[MODRUNTIME] Extra RAM pool ready base=" << Log::Hex
              << g_extra_ram_pool_base_addr << " size=" << g_extra_ram_pool_size
              << " first_alloc_addr=deferred" << Log::Dec << "\n";
    } else {
        Log() << "[MODRUNTIME][WARNING] Extra RAM probe failed at base=" << Log::Hex
              << g_extra_ram_pool_base_addr << " expected=" << kExtraRamProbePattern
              << " got=" << g_debug_state.extram_probe_readback << Log::Dec << "\n";
    }

    SetupHostApi();
    if (TryLoadAndStartOverlay()) {
        g_mod_runtime_mode = ModRuntimeMode::DSI_MODE_OVERLAY_READY;
        Log() << "[MODRUNTIME][OVERLAY] Overlay ready exports=" << Log::Hex
              << reinterpret_cast<uintptr_t>(g_overlay_exports) << Log::Dec << "\n";
    } else {
        Log() << "[MODRUNTIME][OVERLAY] Overlay not ready reason="
              << g_debug_state.overlay_skip_reason << " ("
              << OverlaySkipReasonLabelInternal(g_debug_state.overlay_skip_reason) << ")\n";
    }
    g_debug_state.last_mode_snapshot = g_mod_runtime_mode;
}

extern "C" ncp_call(0x0204c93c) void *ModRuntime_SpawnDispatch(uint16_t object_id,
                                                                void *parent_node,
                                                                uint32_t settings,
                                                                uint8_t base_type) {
    g_debug_state.spawn_dispatch_calls++;
    g_debug_state.last_object_id = object_id;
    g_debug_state.last_settings = settings;
    g_debug_state.last_base_type = base_type;
    g_debug_state.last_mode_snapshot = g_mod_runtime_mode;

    if (!ModRuntime_IsOverlayReady() || g_overlay_exports == nullptr ||
        g_overlay_exports->spawn_custom == nullptr) {
        g_debug_state.fallback_spawn_calls++;
        return BaseSpawnOriginal(object_id, parent_node, settings, base_type);
    }

    if (object_id < kCustomObjectMin || object_id > kCustomObjectMax) {
        g_debug_state.fallback_spawn_calls++;
        return BaseSpawnOriginal(object_id, parent_node, settings, base_type);
    }

    g_debug_state.spawn_custom_id_attempts++;
    g_debug_state.last_custom_object_id = object_id;

    if (g_spawn_in_progress) {
        g_debug_state.fallback_spawn_calls++;
        return BaseSpawnOriginal(object_id, parent_node, settings, base_type);
    }

    g_spawn_in_progress = true;
    g_debug_state.overlay_spawn_attempts++;
    void *spawned = g_overlay_exports->spawn_custom(object_id, parent_node, settings, base_type);
    g_spawn_in_progress = false;

    if (spawned != nullptr) {
        g_debug_state.overlay_spawn_successes++;
        return spawned;
    }
    g_debug_state.fallback_spawn_calls++;
    return BaseSpawnOriginal(object_id, parent_node, settings, base_type);
}
