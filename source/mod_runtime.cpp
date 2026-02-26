#include <cstddef>
#include <cstdint>

#include "nsmb.hpp"

#include "mod_runtime.hpp"

namespace {

constexpr uintptr_t kScfgA9RomAddr = 0x04004000;
constexpr uintptr_t kScfgExt9Addr = 0x04004008;
constexpr uintptr_t kSndExCntAddr = 0x04004700;
constexpr uint32_t kArchiveFileIdBase = 131;

constexpr uintptr_t kBaseSpawnAddr = 0x0204c9a4;
constexpr uintptr_t kFsConvertPathToFileIdAddr = 0x0206a54c;
constexpr uintptr_t kFsInitFileAddr = 0x0206a778;
constexpr uintptr_t kFsOpenFileFastAddr = 0x0206a478;
constexpr uintptr_t kFsReadFileAddr = 0x0206a2f0;
constexpr uintptr_t kFsCloseFileAddr = 0x0206a3e0;
constexpr uintptr_t kFsLoadExtFileToDestAddr = 0x020088fc;
constexpr uintptr_t kFsCacheSetup3DFileAddr = 0x02009DEC;
constexpr uintptr_t kDcStoreRangeAddr = 0x02065ccc;
constexpr uintptr_t kDcFlushRangeAddr = 0x02065ce8;
constexpr uintptr_t kDcWaitWriteBufferEmptyAddr = 0x02065d0c;
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
constexpr uint32_t kMagicNsbtx = 0x30585442;  // "BTX0"
constexpr uint32_t kMagicNsbmd = 0x30444D42;  // "BMD0"

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
constexpr uint32_t kDsiPromoteMinSize = 0x2000;  // 8KB

constexpr uint16_t kCustomObjectMin = 0x0300;
constexpr uint16_t kCustomObjectMax = 0x03ff;
constexpr const char* kOverlayPaths[] = {
    "/z_new/mod/ordinaryovl.bin",
    "/z_new/mod/modovl.bin",
};

struct RuntimeFSFileID {
    void *archive;
    uint32_t file_id;
};

// Size derived from stack usage in readFileIntoBufferWithMax (FStack_68).
struct FSFile {
    uint8_t raw[0x68];
};

using BaseSpawnFn = void *(*)(uint16_t object_id, void *parent_node, uint32_t settings,
                              uint8_t base_type);
using FSConvertPathToFileIDFn = int (*)(RuntimeFSFileID *id, char *path);
using FSInitFileFn = void (*)(FSFile *file);
using FSOpenFileFastFn = int (*)(FSFile *file, int archive, uint32_t file_id);
using FSReadFileFn = int (*)(FSFile *file, void *dest, uint32_t size);
using FSCloseFileFn = int (*)(FSFile *file);
using FSLoadExtFileToDestFn = int (*)(uint32_t ext_file_id, void *dest, int size);
using FSCacheSetup3DFileFn = bool (*)(void *file, bool unload_textures);
using DCStoreRangeFn = void (*)(const void *start, int size);
using DCFlushRangeFn = void (*)(void *start, int size);
using DCWaitWriteBufferEmptyFn = void (*)();
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
FSLoadExtFileToDestFn FSLoadExtFileToDest =
    reinterpret_cast<FSLoadExtFileToDestFn>(kFsLoadExtFileToDestAddr);
FSCacheSetup3DFileFn FSCacheSetup3DFile =
    reinterpret_cast<FSCacheSetup3DFileFn>(kFsCacheSetup3DFileAddr);
DCStoreRangeFn DCStoreRange = reinterpret_cast<DCStoreRangeFn>(kDcStoreRangeAddr);
DCFlushRangeFn DCFlushRange = reinterpret_cast<DCFlushRangeFn>(kDcFlushRangeAddr);
DCWaitWriteBufferEmptyFn DCWaitWriteBufferEmpty =
    reinterpret_cast<DCWaitWriteBufferEmptyFn>(kDcWaitWriteBufferEmptyAddr);
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
bool g_extra_ram_reset_pending = false;
uint32_t g_extra_ram_pool_cursor = 0;
uint32_t g_extra_ram_generation = 1;
uintptr_t g_extra_ram_pool_base_addr = kExtraRamPoolStrictBaseAddr;
uint32_t g_extra_ram_pool_size = kExtraRamPoolStrictSize;

struct PromotedFileEntry {
    uint32_t ext_file_id;
    uint32_t magic;
    uint32_t size;
    void *data;
};

constexpr uint32_t kMaxPromotedFiles = 64;
PromotedFileEntry g_promoted_files[kMaxPromotedFiles] = {};
uint32_t g_promoted_file_slots_used = 0;

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

void CopyBytes(void *dest, const void *src, uint32_t size) {
    auto *dst = reinterpret_cast<uint8_t *>(dest);
    auto *source = reinterpret_cast<const uint8_t *>(src);
    for (uint32_t i = 0; i < size; ++i) {
        dst[i] = source[i];
    }
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

bool HasExpectedMagic(const void *file, uint32_t expected_magic) {
    return file != nullptr && (*reinterpret_cast<const uint32_t *>(file) == expected_magic);
}

bool Is3DResourceMagic(uint32_t magic) {
    return magic == kMagicNsbtx || magic == kMagicNsbmd;
}

uint32_t ReadFileMagic(const void *file) {
    return (file == nullptr) ? 0u : *reinterpret_cast<const uint32_t *>(file);
}

void BumpExtraRamGeneration() {
    g_extra_ram_generation++;
    if (g_extra_ram_generation == 0u) {
        g_extra_ram_generation = 1u;
    }
}

void ResetPromotedFiles() {
    g_promoted_file_slots_used = 0;
    for (uint32_t i = 0; i < kMaxPromotedFiles; ++i) {
        g_promoted_files[i].ext_file_id = 0xffffffffu;
        g_promoted_files[i].magic = 0;
        g_promoted_files[i].size = 0;
        g_promoted_files[i].data = nullptr;
    }
}

PromotedFileEntry *FindPromotedEntry(uint32_t ext_file_id) {
    for (uint32_t i = 0; i < kMaxPromotedFiles; ++i) {
        PromotedFileEntry &entry = g_promoted_files[i];
        if (entry.data == nullptr) {
            continue;
        }
        if (entry.ext_file_id == ext_file_id) {
            return &entry;
        }
    }
    return nullptr;
}

const PromotedFileEntry *FindPromotedEntryConst(uint32_t ext_file_id) {
    for (uint32_t i = 0; i < kMaxPromotedFiles; ++i) {
        const PromotedFileEntry &entry = g_promoted_files[i];
        if (entry.data == nullptr) {
            continue;
        }
        if (entry.ext_file_id == ext_file_id) {
            return &entry;
        }
    }
    return nullptr;
}

void *FindPromotedFile(uint32_t ext_file_id, uint32_t expected_magic) {
    const PromotedFileEntry *entry = FindPromotedEntryConst(ext_file_id);
    if (entry == nullptr) {
        return nullptr;
    }
    return HasExpectedMagic(entry->data, expected_magic) ? entry->data : nullptr;
}

void RememberPromotedFile(uint32_t ext_file_id, void *data, uint32_t size) {
    if (data == nullptr) {
        return;
    }

    const uint32_t magic = ReadFileMagic(data);
    uint32_t free_slot = 0xffffffffu;
    for (uint32_t i = 0; i < kMaxPromotedFiles; ++i) {
        PromotedFileEntry &entry = g_promoted_files[i];
        if (entry.data == nullptr && free_slot == 0xffffffffu) {
            free_slot = i;
            continue;
        }
        if (entry.ext_file_id == ext_file_id) {
            entry.magic = magic;
            entry.size = size;
            entry.data = data;
            return;
        }
    }

    if (free_slot == 0xffffffffu) {
        return;
    }

    g_promoted_files[free_slot].ext_file_id = ext_file_id;
    g_promoted_files[free_slot].magic = magic;
    g_promoted_files[free_slot].size = size;
    g_promoted_files[free_slot].data = data;
    if (g_promoted_file_slots_used < kMaxPromotedFiles) {
        g_promoted_file_slots_used++;
    }
}

bool ShouldPromoteForPolicy(ModRuntimeDsiFilePolicy policy, uint32_t size) {
    switch (policy) {
        case ModRuntimeDsiFilePolicy::PROMOTE_ALWAYS:
            return size != 0u;
        case ModRuntimeDsiFilePolicy::PROMOTE_IF_LARGE:
            return size >= kDsiPromoteMinSize;
        default:
            return false;
    }
}

bool ReadExtFileToBuffer(uint32_t ext_file_id, void *dest, uint32_t size) {
    if (dest == nullptr || size == 0u) {
        return false;
    }
    const int read = FSLoadExtFileToDest(ext_file_id, dest, static_cast<int>(size));
    return read == static_cast<int>(size);
}

bool FinalizePromotedFile(void *data, uint32_t size) {
    if (data == nullptr || size == 0u) {
        return false;
    }

    DCStoreRange(data, static_cast<int>(size));
    DCWaitWriteBufferEmpty();

    const uint32_t magic = ReadFileMagic(data);
    if (!Is3DResourceMagic(magic)) {
        return true;
    }

    if (!FSCacheSetup3DFile(data, false)) {
        return false;
    }

    DCStoreRange(data, static_cast<int>(size));
    DCWaitWriteBufferEmpty();
    return true;
}

void ResetExtraRamPoolState() {
    g_extra_ram_pool_ready = false;
    g_extra_ram_reset_pending = false;
    g_extra_ram_pool_cursor = 0;
    g_extra_ram_generation = 1u;
    ResetPromotedFiles();
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
    RuntimeFSFileID file_id = {};
    bool found = false;
    for (const char* path : kOverlayPaths) {
        if (FSConvertPathToFileID(&file_id, const_cast<char*>(path)) != 0) {
            found = true;
            break;
        }
    }
    if (!found) {
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
    DCWaitWriteBufferEmpty();
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

void NotifyOverlaySceneChange(uint16_t scene_id) {
    g_debug_state.scene_change_calls++;
    g_debug_state.last_scene_change_id = scene_id;

    if (g_overlay_exports == nullptr || g_overlay_exports->on_scene_change == nullptr) {
        return;
    }

    g_debug_state.overlay_scene_callbacks++;
    g_overlay_exports->on_scene_change(scene_id);
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

bool ModRuntime_TryPromoteLoadedFile(uint32_t ext_file_id, const void *source_data, uint32_t source_size,
                                     ModRuntimeDsiFileLoadResult *out_result) {
    if (out_result == nullptr) {
        return false;
    }
    out_result->data = nullptr;
    out_result->size = 0;
    out_result->magic = 0;
    out_result->reused = false;

    if (!g_extra_ram_pool_ready || source_data == nullptr || source_size == 0u) {
        return false;
    }

    if (PromotedFileEntry *cached = FindPromotedEntry(ext_file_id)) {
        out_result->data = cached->data;
        out_result->size = cached->size;
        out_result->magic = cached->magic;
        out_result->reused = true;
        return true;
    }

    void *promoted = ModRuntime_ExtraRamAlloc(source_size, 32);
    if (promoted == nullptr) {
        return false;
    }

    CopyBytes(promoted, source_data, source_size);
    if (!FinalizePromotedFile(promoted, source_size)) {
        return false;
    }

    RememberPromotedFile(ext_file_id, promoted, source_size);
    out_result->data = promoted;
    out_result->size = source_size;
    out_result->magic = ReadFileMagic(promoted);
    return true;
}

bool ModRuntime_TryLoadFileToExtraRam(uint32_t ext_file_id, ModRuntimeDsiFilePolicy policy,
                                      ModRuntimeDsiFileLoadResult *out_result) {
    if (out_result == nullptr) {
        return false;
    }
    out_result->data = nullptr;
    out_result->size = 0;
    out_result->magic = 0;
    out_result->reused = false;

    if (!g_extra_ram_pool_ready) {
        return false;
    }

    const uint32_t file_size = FS::getFileSize(ext_file_id);
    if (!ShouldPromoteForPolicy(policy, file_size)) {
        return false;
    }

    if (PromotedFileEntry *cached = FindPromotedEntry(ext_file_id)) {
        out_result->data = cached->data;
        out_result->size = cached->size;
        out_result->magic = cached->magic;
        out_result->reused = true;
        return true;
    }

    void *promoted = ModRuntime_ExtraRamAlloc(file_size, 32);
    if (promoted == nullptr) {
        return false;
    }
    if (!ReadExtFileToBuffer(ext_file_id, promoted, file_size)) {
        return false;
    }
    if (!FinalizePromotedFile(promoted, file_size)) {
        return false;
    }

    RememberPromotedFile(ext_file_id, promoted, file_size);
    out_result->data = promoted;
    out_result->size = file_size;
    out_result->magic = ReadFileMagic(promoted);
    return true;
}

bool ModRuntime_LoadValidatedFile(const char *path, uint32_t expected_magic, uint32_t *out_ext_file_id,
                                  void **out_file) {
    if (path == nullptr || out_ext_file_id == nullptr || out_file == nullptr) {
        return false;
    }

    *out_ext_file_id = 0;
    *out_file = nullptr;

    RuntimeFSFileID file_id = {};
    if (FSConvertPathToFileID(&file_id, const_cast<char *>(path)) == 0) {
        return false;
    }

    const uint32_t candidates[2] = {
        (file_id.file_id >= kArchiveFileIdBase) ? (file_id.file_id - kArchiveFileIdBase) : 0xffffffffu,
        file_id.file_id,
    };

    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t ext_file_id = candidates[i];
        if (ext_file_id == 0xffffffffu) {
            continue;
        }

        if (g_extra_ram_pool_ready) {
            void *cached_promoted = FindPromotedFile(ext_file_id, expected_magic);
            if (cached_promoted != nullptr) {
                *out_ext_file_id = ext_file_id;
                *out_file = cached_promoted;
                return true;
            }
        }

        void *cached_file = FS::Cache::loadFile(ext_file_id, false);
        if (!HasExpectedMagic(cached_file, expected_magic)) {
            continue;
        }

        void *file = cached_file;
        if (g_extra_ram_pool_ready) {
            const uint32_t file_size = FS::getFileSize(ext_file_id);
            ModRuntimeDsiFileLoadResult promoted = {};
            if (file_size != 0u &&
                ModRuntime_TryPromoteLoadedFile(ext_file_id, cached_file, file_size, &promoted)) {
                if (HasExpectedMagic(promoted.data, expected_magic)) {
                    FS::Cache::unloadFile(ext_file_id);
                    file = promoted.data;
                } else {
                    Log() << "[MODRUNTIME][FS] promoted magic mismatch extFileId=" << ext_file_id
                          << " expected=" << Log::Hex << expected_magic
                          << " got=" << promoted.magic << Log::Dec << "\n";
                }
            }
        }

        if (!HasExpectedMagic(file, expected_magic)) {
            continue;
        }

        *out_ext_file_id = ext_file_id;
        *out_file = file;
        return true;
    }

    return false;
}

void ModRuntime_ExtraRamReset() {
    g_extra_ram_pool_cursor = 0;
    g_extra_ram_reset_pending = false;
    ResetPromotedFiles();
    g_debug_state.extram_pool_cursor = 0;
    BumpExtraRamGeneration();
}

uint32_t ModRuntime_GetExtraRamGeneration() {
    return g_extra_ram_generation;
}

uint32_t ModRuntime_GetPromotedFileCount() {
    return g_promoted_file_slots_used;
}

void ModRuntime_NotifySceneChange(uint16_t scene_id) {
    // Area-switch hooks can fire while old area assets are still live.
    // Defer reset until an explicit safe boundary (area enter / overlay unload).
    if (scene_id == kModRuntimeSceneEventSwitchArea ||
        scene_id == kModRuntimeSceneEventAreaLeave) {
        if (!g_extra_ram_reset_pending) {
            Log() << "[DSI-MEM][LIFECYCLE] deferred reset pending scene="
                  << Log::Hex << scene_id << Log::Dec << "\n";
        }
        g_extra_ram_reset_pending = true;
    }

    const bool safe_reset_boundary =
        scene_id == kModRuntimeSceneEventAreaEnter ||
        scene_id == kModRuntimeSceneEventOverlayUnload;
    if (safe_reset_boundary && g_extra_ram_pool_ready &&
        (g_extra_ram_reset_pending || scene_id == kModRuntimeSceneEventOverlayUnload)) {
        Log() << "[DSI-MEM][LIFECYCLE] applying deferred reset scene="
              << Log::Hex << scene_id << Log::Dec << "\n";
        ModRuntime_ExtraRamReset();
    }
    NotifyOverlaySceneChange(scene_id);
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
