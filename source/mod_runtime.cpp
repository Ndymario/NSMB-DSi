#include <cstddef>
#include <cstdint>

#include "nsmb.hpp"

#include "mod_runtime.hpp"

namespace {

constexpr uintptr_t kScfgA9RomAddr = 0x04004000;
constexpr uintptr_t kScfgExt9Addr = 0x04004008;
constexpr uintptr_t kSndExCntAddr = 0x04004700;
constexpr uintptr_t kExMemCntAddr = 0x04000204;
constexpr uintptr_t kOperaSlot2UnlockAddr = 0x08240000;
constexpr uint32_t kArchiveFileIdBase = 131;

constexpr uintptr_t kBaseSpawnAddr = 0x0204c9a4;
constexpr uintptr_t kFsConvertPathToFileIdAddr = 0x0206a54c;
constexpr uintptr_t kFsInitFileAddr = 0x0206a778;
constexpr uintptr_t kFsOpenFileFastAddr = 0x0206a478;
constexpr uintptr_t kFsReadFileAddr = 0x0206a2f0;
constexpr uintptr_t kFsCloseFileAddr = 0x0206a3e0;
constexpr uintptr_t kFsLoadExtFileToDestAddr = 0x020088fc;
constexpr uintptr_t kFsCacheClearAddr = 0x02009B64;
constexpr uintptr_t kFsCacheSetup3DFileAddr = 0x02009DEC;
constexpr uintptr_t kFsCacheInternalClearAddr = 0x02009D30;
constexpr uintptr_t kFsCacheActiveFileCacheAddr = 0x02085E0C;
constexpr uintptr_t kFsCacheFileCache0Addr = 0x02086A30;
constexpr uintptr_t kFsCacheFileCache1Addr = 0x02085E30;
constexpr uintptr_t kDcStoreRangeAddr = 0x02065ccc;
constexpr uintptr_t kDcFlushRangeAddr = 0x02065ce8;
constexpr uintptr_t kDcWaitWriteBufferEmptyAddr = 0x02065d0c;
constexpr uintptr_t kIcInvalidateRangeAddr = 0x02065d24;
constexpr uintptr_t kOsEnableProtectionUnitAddr = 0x02066230;
constexpr uintptr_t kOsSetProtectionRegion1Addr = 0x02066250;
constexpr uint32_t kExpansionOverlayPuRegionIndex = 3u;

constexpr uintptr_t kDsiOverlayImageBaseAddr = 0x02800000;
constexpr uintptr_t kDsiOverlayImageEndAddr = 0x028fffff;
constexpr uintptr_t kDsiOverlayHeapBaseAddr = 0x02900000;
constexpr uintptr_t kDsiOverlayGuardBaseAddr = 0x02c00000;
constexpr uintptr_t kDsiExtraRamPoolBaseAddr = 0x02c10000;
constexpr uintptr_t kDsiExtraRamPoolEndAddr = 0x02ffffff;

constexpr uintptr_t kExpansionOverlayImageBaseAddr = 0x09000000;
constexpr uintptr_t kExpansionOverlayImageEndAddr = 0x091fffff;
constexpr uintptr_t kExpansionOverlayHeapBaseAddr = 0x09200000;
constexpr uintptr_t kExpansionOverlayGuardBaseAddr = 0x09800000;
constexpr uintptr_t kExpansionExtraRamPoolBaseAddr = 0x09200000;
constexpr uintptr_t kExpansionExtraRamPoolEndAddr = 0x097fffff;

constexpr uint32_t kExtraRamProbePattern = 0x584D454Du;  // "XMEM"
constexpr uint16_t kScfgA9RomDsiMode = 0x0001;
constexpr uint32_t kMagicNsbtx = 0x30585442;  // "BTX0"
constexpr uint32_t kMagicNsbmd = 0x30444D42;  // "BMD0"
constexpr uint32_t kExMemCntSlot2Arm7 = 1u << 7;

// Ghidra: OS_InitArenaEx writes 0x0200002B to region1 (4MB). 16MB is size-field +2.
constexpr uint32_t kMpuMainRamRegion4Mb = 0x0200002b;
constexpr uint32_t kMpuMainRamRegion16Mb = 0x0200002f;
static_assert(kMpuMainRamRegion16Mb == (kMpuMainRamRegion4Mb + 4u),
              "MPU main RAM region16 descriptor should be the 4MB descriptor with a larger size.");

static_assert(kDsiExtraRamPoolBaseAddr > kDsiOverlayGuardBaseAddr,
              "DSi extra RAM pool must be above overlay guard.");

constexpr uint32_t kDsiPromoteMinSize = 0x2000;  // 8KB

constexpr uint16_t kCustomObjectMin = 0x0300;
constexpr uint16_t kCustomObjectMax = 0x03ff;
constexpr const char *kDsiOverlayPaths[] = {
    "/z_new/mod/ordinaryovl.bin",
    "/z_new/mod/modovl.bin",
};
constexpr const char *kExpansionOverlayPaths[] = {
    "/z_new/mod/ordinaryovl_expansion.bin",
};

struct RuntimeMemoryLayout {
    ModRuntimeMemoryTier memory_tier;
    uintptr_t overlay_image_base_addr;
    uintptr_t overlay_image_end_addr;
    uintptr_t overlay_heap_base_addr;
    uintptr_t overlay_guard_base_addr;
    uintptr_t extra_ram_pool_base_addr;
    uintptr_t extra_ram_pool_end_addr;
    const char *const *overlay_paths;
    uint32_t overlay_path_count;
};

constexpr RuntimeMemoryLayout kDsiMemoryLayout = {
    ModRuntimeMemoryTier::DSI,
    kDsiOverlayImageBaseAddr,
    kDsiOverlayImageEndAddr,
    kDsiOverlayHeapBaseAddr,
    kDsiOverlayGuardBaseAddr,
    kDsiExtraRamPoolBaseAddr,
    kDsiExtraRamPoolEndAddr,
    kDsiOverlayPaths,
    sizeof(kDsiOverlayPaths) / sizeof(kDsiOverlayPaths[0]),
};

constexpr RuntimeMemoryLayout kExpansionMemoryLayout = {
    ModRuntimeMemoryTier::EXPANSION_PAK,
    kExpansionOverlayImageBaseAddr,
    kExpansionOverlayImageEndAddr,
    kExpansionOverlayHeapBaseAddr,
    kExpansionOverlayGuardBaseAddr,
    kExpansionExtraRamPoolBaseAddr,
    kExpansionExtraRamPoolEndAddr,
    kExpansionOverlayPaths,
    sizeof(kExpansionOverlayPaths) / sizeof(kExpansionOverlayPaths[0]),
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
using FSCacheClearFn = void (*)();
using FSCacheSetup3DFileFn = bool (*)(void *file, bool unload_textures);
using FSCacheInternalClearFn = void (*)(FS::Cache::CacheEntry *cache, uint32_t entries);
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
FSCacheClearFn FSCacheClear = reinterpret_cast<FSCacheClearFn>(kFsCacheClearAddr);
FSCacheSetup3DFileFn FSCacheSetup3DFile =
    reinterpret_cast<FSCacheSetup3DFileFn>(kFsCacheSetup3DFileAddr);
FSCacheInternalClearFn FSCacheInternalClear =
    reinterpret_cast<FSCacheInternalClearFn>(kFsCacheInternalClearAddr);
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

const RuntimeMemoryLayout *g_active_memory_layout = nullptr;
uint8_t *g_overlay_image_base = nullptr;
ModRuntimeMemoryTier g_mod_runtime_memory_tier = ModRuntimeMemoryTier::NONE;
ModRuntimeMode g_mod_runtime_mode = ModRuntimeMode::UNINITIALIZED;
ModRuntimeDebugState g_debug_state = {};
bool g_bootstrap_finished = false;
bool g_spawn_in_progress = false;
bool g_logged_detect_result = false;
bool g_extra_ram_pool_ready = false;
bool g_extra_ram_reset_pending = false;
uint32_t g_extra_ram_pool_cursor = 0;
uint32_t g_extra_ram_generation = 1;
uintptr_t g_extra_ram_pool_base_addr = 0;
uint32_t g_extra_ram_pool_size = 0;
uint32_t g_prebootstrap_expansion_reserved_top = 0;
bool g_prebootstrap_expansion_available = false;
bool g_prebootstrap_expansion_checked = false;

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
constexpr uint32_t kDetectFailExpansionBusOwner = 4;
constexpr uint32_t kDetectFailMpuConfig = 5;
constexpr uint32_t kDetectFailExpansionUnlock = 6;
constexpr uint32_t kDetectFailExpansionProbe = 7;

constexpr uint32_t kExtraRamFailStageNone = 0;
constexpr uint32_t kExtraRamFailStageTwlExt9Bit31 = 1;
constexpr uint32_t kExtraRamFailStageTwlA9Rom = 2;
constexpr uint32_t kExtraRamFailStageTwlRamLimit = 3;
constexpr uint32_t kExtraRamFailStageTwlMpu = 4;
constexpr uint32_t kExtraRamFailStageExpansionBusOwner = 5;
constexpr uint32_t kExtraRamFailStageExpansionUnlock = 6;
constexpr uint32_t kExtraRamFailStageExpansionProbe = 7;

constexpr uint32_t kOverlaySkipNone = 0;
constexpr uint32_t kOverlaySkipNoEnhancedMemory = 1;
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
        case kDetectFailExpansionBusOwner:
            return "slot2_not_owned_by_arm9";
        case kDetectFailMpuConfig:
            return "mpu_region1_not_16mb";
        case kDetectFailExpansionUnlock:
            return "slot2_unlock_failed";
        case kDetectFailExpansionProbe:
            return "slot2_ram_probe_failed";
        default:
            return "unknown";
    }
}

const char *OverlaySkipReasonLabelInternal(uint32_t code) {
    switch (code) {
        case kOverlaySkipNone:
            return "none";
        case kOverlaySkipNoEnhancedMemory:
            return "no_enhanced_memory";
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

inline void WriteU16(uintptr_t addr, uint16_t value) {
    *reinterpret_cast<volatile uint16_t *>(addr) = value;
}

inline void WriteU32(uintptr_t addr, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(addr) = value;
}

uint32_t GetOverlayMaxImageSize() {
    if (g_active_memory_layout == nullptr) {
        return 0;
    }
    return static_cast<uint32_t>(g_active_memory_layout->overlay_image_end_addr -
                                 g_active_memory_layout->overlay_image_base_addr + 1u);
}

uint32_t GetOverlayTotalRegionSize() {
    if (g_active_memory_layout == nullptr) {
        return 0;
    }
    return static_cast<uint32_t>(g_active_memory_layout->overlay_guard_base_addr -
                                 g_active_memory_layout->overlay_image_base_addr);
}

uint32_t GetOverlayMaxBssSize() {
    const uint32_t image_size = GetOverlayMaxImageSize();
    const uint32_t total_size = GetOverlayTotalRegionSize();
    return (total_size >= image_size) ? (total_size - image_size) : 0u;
}

uint32_t ReadMpuMainRamRegionValue() {
    uint32_t value = 0;
    asm volatile("mrc p15, 0, %0, c6, c1, 0" : "=r"(value));
    return value;
}

uint32_t ReadPuRegionValue(uint32_t region_index) {
    uint32_t value = 0;
    switch (region_index) {
        case 0: asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(value)); break;
        case 1: asm volatile("mrc p15, 0, %0, c6, c1, 0" : "=r"(value)); break;
        case 2: asm volatile("mrc p15, 0, %0, c6, c2, 0" : "=r"(value)); break;
        case 3: asm volatile("mrc p15, 0, %0, c6, c3, 0" : "=r"(value)); break;
        case 4: asm volatile("mrc p15, 0, %0, c6, c4, 0" : "=r"(value)); break;
        case 5: asm volatile("mrc p15, 0, %0, c6, c5, 0" : "=r"(value)); break;
        case 6: asm volatile("mrc p15, 0, %0, c6, c6, 0" : "=r"(value)); break;
        case 7: asm volatile("mrc p15, 0, %0, c6, c7, 0" : "=r"(value)); break;
        default: break;
    }
    return value;
}

uint32_t ReadPuInstructionPermissions() {
    uint32_t value = 0;
    asm volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(value));
    return value;
}

void WritePuInstructionPermissions(uint32_t value) {
    asm volatile("mcr p15, 0, %0, c5, c0, 1" : : "r"(value));
}

uint32_t ReadPuExtendedInstructionPermissions() {
    uint32_t value = 0;
    asm volatile("mrc p15, 0, %0, c5, c0, 3" : "=r"(value));
    return value;
}

void WritePuExtendedInstructionPermissions(uint32_t value) {
    asm volatile("mcr p15, 0, %0, c5, c0, 3" : : "r"(value));
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
    const uintptr_t dest_addr = reinterpret_cast<uintptr_t>(dest);
    const bool is_extra_ram_dest =
        g_extra_ram_pool_size != 0u && dest_addr >= g_extra_ram_pool_base_addr &&
        dest_addr < (g_extra_ram_pool_base_addr + g_extra_ram_pool_size);

    auto *dst8 = reinterpret_cast<uint8_t *>(dest);
    auto *source8 = reinterpret_cast<const uint8_t *>(src);
    if (!is_extra_ram_dest) {
        for (uint32_t i = 0; i < size; ++i) {
            dst8[i] = source8[i];
        }
        return;
    }

    auto *dst32 = reinterpret_cast<volatile uint32_t *>(dest);
    const auto *source32 = reinterpret_cast<const uint32_t *>(src);
    uint32_t copied = 0u;

    while ((copied + 4u) <= size) {
        *dst32++ = *source32++;
        copied += 4u;
    }

    if ((size - copied) >= 2u) {
        *reinterpret_cast<volatile uint16_t *>(reinterpret_cast<uintptr_t>(dst8 + copied)) =
            *reinterpret_cast<const uint16_t *>(source8 + copied);
        copied += 2u;
    }

    if (copied < size) {
        const uint16_t tail = source8[copied];
        *reinterpret_cast<volatile uint16_t *>(reinterpret_cast<uintptr_t>(dst8 + copied)) = tail;
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

uintptr_t AlignDown(uintptr_t value, uint32_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const uintptr_t mask = static_cast<uintptr_t>(alignment - 1u);
    return value & ~mask;
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

bool ShouldPromoteForPolicy(ModRuntimeExtraRamFilePolicy policy, uint32_t size) {
    switch (policy) {
        case ModRuntimeExtraRamFilePolicy::PROMOTE_ALWAYS:
            return size != 0u;
        case ModRuntimeExtraRamFilePolicy::PROMOTE_IF_LARGE:
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

    if (!FSCacheSetup3DFile(data, true)) {
        return false;
    }

    DCStoreRange(data, static_cast<int>(size));
    DCWaitWriteBufferEmpty();
    return true;
}

void ClearCacheTableForReset(uint16_t scene_id) {
    if (scene_id == kModRuntimeSceneEventOverlayUnload) {
        FSCacheClear();
        Log() << "[DSI-MEM][LIFECYCLE] cleared all cache tables scene=" << Log::Hex
              << scene_id << Log::Dec << "\n";
        return;
    }

    volatile uint32_t *const active_cache =
        reinterpret_cast<volatile uint32_t *>(kFsCacheActiveFileCacheAddr);
    const uint32_t active_index = *active_cache;
    auto *const cache0 = reinterpret_cast<FS::Cache::CacheEntry *>(kFsCacheFileCache0Addr);
    auto *const cache1 = reinterpret_cast<FS::Cache::CacheEntry *>(kFsCacheFileCache1Addr);

    FS::Cache::CacheEntry *cache_to_clear = nullptr;
    uint32_t cleared_index = 0xffffffffu;

    if (scene_id == kModRuntimeSceneEventOverlayUnload) {
        if (active_index == 0u) {
            cache_to_clear = cache0;
            cleared_index = 0u;
        } else if (active_index == 1u) {
            cache_to_clear = cache1;
            cleared_index = 1u;
        }
    } else {
        if (active_index == 0u) {
            cache_to_clear = cache1;
            cleared_index = 1u;
        } else if (active_index == 1u) {
            cache_to_clear = cache0;
            cleared_index = 0u;
        }
    }

    if (cache_to_clear == nullptr) {
        Log() << "[DSI-MEM][LIFECYCLE] cache clear skipped scene=" << Log::Hex << scene_id
              << Log::Dec << " active=" << active_index << "\n";
        return;
    }

    FSCacheInternalClear(cache_to_clear, 128);
    Log() << "[DSI-MEM][LIFECYCLE] cleared cache table scene=" << Log::Hex << scene_id
          << Log::Dec << " active=" << active_index
          << " cleared=" << cleared_index << "\n";
}

void SetActiveMemoryLayout(const RuntimeMemoryLayout *layout) {
    g_active_memory_layout = layout;
    if (layout == nullptr) {
        g_mod_runtime_memory_tier = ModRuntimeMemoryTier::NONE;
        g_overlay_image_base = nullptr;
        g_extra_ram_pool_base_addr = 0;
        g_extra_ram_pool_size = 0;
        g_debug_state.overlay_image_base = 0;
        g_debug_state.overlay_image_size = 0;
        g_debug_state.extram_pool_memory_tier = 0;
        return;
    }

    g_mod_runtime_memory_tier = layout->memory_tier;
    g_overlay_image_base = reinterpret_cast<uint8_t *>(layout->overlay_image_base_addr);
    g_extra_ram_pool_base_addr = layout->extra_ram_pool_base_addr;
    uintptr_t pool_end_addr = layout->extra_ram_pool_end_addr;
    if (layout->memory_tier == ModRuntimeMemoryTier::EXPANSION_PAK &&
        g_prebootstrap_expansion_reserved_top != 0u &&
        g_prebootstrap_expansion_reserved_top <
            (layout->extra_ram_pool_end_addr - layout->extra_ram_pool_base_addr + 1u)) {
        pool_end_addr -= g_prebootstrap_expansion_reserved_top;
    }
    g_extra_ram_pool_size = static_cast<uint32_t>(pool_end_addr -
                                                  layout->extra_ram_pool_base_addr + 1u);
    g_debug_state.overlay_image_base = static_cast<uint32_t>(layout->overlay_image_base_addr);
    g_debug_state.overlay_image_size = GetOverlayMaxImageSize();
    g_debug_state.extram_pool_memory_tier = static_cast<uint32_t>(layout->memory_tier);
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
        (g_debug_state.dsi_hw_likely != 0u && !extra_ram_ready &&
         g_mod_runtime_memory_tier == ModRuntimeMemoryTier::NONE) ? 1u : 0u;
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
          << " FAIL_STAGE=" << g_debug_state.extra_ram_fail_stage
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

bool ClaimSlot2ForArm9() {
    const uint16_t before = ReadU16(kExMemCntAddr);
    g_debug_state.raw_exmemcnt = before;
    WriteU16(kExMemCntAddr, before & ~kExMemCntSlot2Arm7);
    const uint16_t after = ReadU16(kExMemCntAddr);
    g_debug_state.raw_exmemcnt = after;
    return (after & kExMemCntSlot2Arm7) == 0u;
}

bool ProbeExpansionPakBase() {
    volatile uint32_t *const probe =
        reinterpret_cast<volatile uint32_t *>(kExpansionExtraRamPoolBaseAddr);
    const uint32_t saved = *probe;
    *probe = kExtraRamProbePattern;
    const uint32_t readback = *probe;
    *probe = saved;
    return readback == kExtraRamProbePattern;
}

bool UnlockOperaExpansionPak() {
    WriteU32(kOperaSlot2UnlockAddr, 1u);
    const uint32_t value = ReadU32(kOperaSlot2UnlockAddr);
    g_debug_state.raw_slot2_unlock = value;
    if ((value & 1u) != 0u) {
        return true;
    }

    // The unlock register echo is not reliable once Slot-2 RAM has already been
    // opened earlier in boot. In that case, trust a direct RAM probe instead of
    // forcing the runtime back to plain NDS mode.
    if (ProbeExpansionPakBase()) {
        return true;
    }

    return false;
}

bool EnsurePreBootstrapExpansionAvailable() {
    if (g_prebootstrap_expansion_checked) {
        return g_prebootstrap_expansion_available;
    }

    g_prebootstrap_expansion_checked = true;
    g_prebootstrap_expansion_available =
        ClaimSlot2ForArm9() && UnlockOperaExpansionPak() && ProbeExpansionPakBase();
    return g_prebootstrap_expansion_available;
}

void *ReserveExpansionPakTop(uint32_t size, uint32_t alignment) {
    if (size == 0u || !EnsurePreBootstrapExpansionAvailable()) {
        return nullptr;
    }

    const uintptr_t pool_start = kExpansionExtraRamPoolBaseAddr;
    const uintptr_t pool_end_exclusive = kExpansionExtraRamPoolEndAddr + 1u;
    const uintptr_t current_end = pool_end_exclusive - g_prebootstrap_expansion_reserved_top;
    if (current_end <= pool_start || size > (current_end - pool_start)) {
        return nullptr;
    }

    const uintptr_t candidate = AlignDown(current_end - size, alignment);
    if (candidate < pool_start || candidate >= current_end) {
        return nullptr;
    }

    g_prebootstrap_expansion_reserved_top =
        static_cast<uint32_t>(pool_end_exclusive - candidate);
    return reinterpret_cast<void *>(candidate);
}

bool EnableExpansionOverlayExecution() {
    const uint32_t region_value = ReadPuRegionValue(kExpansionOverlayPuRegionIndex);
    const uint32_t region_base = region_value & 0xFFFFF000u;
    const bool region_enabled = (region_value & 1u) != 0u;
    if (!region_enabled || region_base != 0x08000000u) {
        Log() << "[MODRUNTIME][EXPANSION][WARNING] PU region3 is not mapped to Slot-2 executable space."
              << " region3=" << Log::Hex << region_value << Log::Dec << "\n";
        return false;
    }

    const uint32_t ap_shift = kExpansionOverlayPuRegionIndex * 2u;
    const uint32_t ext_shift = kExpansionOverlayPuRegionIndex * 4u;
    uint32_t instr_ap = ReadPuInstructionPermissions();
    uint32_t instr_ext_ap = ReadPuExtendedInstructionPermissions();

    instr_ap &= ~(0x3u << ap_shift);
    instr_ap |= (0x1u << ap_shift);  // Privileged execute/read allowed, user denied.
    instr_ext_ap &= ~(0xFu << ext_shift);
    instr_ext_ap |= (0x5u << ext_shift);  // Privileged read-only, user denied.

    WritePuInstructionPermissions(instr_ap);
    WritePuExtendedInstructionPermissions(instr_ext_ap);
    OSEnableProtectionUnit();

    const uint32_t applied_ap = ReadPuInstructionPermissions();
    const uint32_t applied_ext_ap = ReadPuExtendedInstructionPermissions();
    const bool applied =
        (((applied_ap >> ap_shift) & 0x3u) == 0x1u) &&
        (((applied_ext_ap >> ext_shift) & 0xFu) == 0x5u);
    if (!applied) {
        Log() << "[MODRUNTIME][EXPANSION][WARNING] Failed to enable executable instruction permissions"
              << " instr_ap=" << Log::Hex << applied_ap
              << " instr_ext=" << applied_ext_ap << Log::Dec << "\n";
    }
    return applied;
}

bool DetectAndEnableDsiExtraRam() {
    DsiRegisterState state = ReadDsiRegisters();
    g_debug_state.dsi_compat_warning_emitted = 0u;
    g_debug_state.expansion_pak_detected = 0u;
    g_debug_state.extra_ram_fail_stage = kExtraRamFailStageNone;
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
    g_debug_state.raw_exmemcnt = ReadU16(kExMemCntAddr);
    g_debug_state.raw_slot2_unlock = 0u;

    SetActiveMemoryLayout(nullptr);
    bool extra_ram_ready = false;
    if (IsStrictTwlReady(state)) {
        SetActiveMemoryLayout(&kDsiMemoryLayout);
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
        g_debug_state.extra_ram_fail_stage = kExtraRamFailStageTwlMpu;
        g_debug_state.detect_fail_reason = kDetectFailMpuConfig;
    } else if (state.ext9_bit31 == 0u) {
        g_debug_state.extra_ram_fail_stage = kExtraRamFailStageTwlExt9Bit31;
        g_debug_state.detect_fail_reason = kDetectFailScfgExt9Bit31;
    } else if (state.ram_limit < 2u) {
        g_debug_state.extra_ram_fail_stage = kExtraRamFailStageTwlRamLimit;
        g_debug_state.detect_fail_reason = kDetectFailRamLimit;
    } else {
        g_debug_state.extra_ram_fail_stage = kExtraRamFailStageTwlA9Rom;
        g_debug_state.detect_fail_reason = kDetectFailScfgA9Rom;
    }

    SetActiveMemoryLayout(&kExpansionMemoryLayout);
    if (!ClaimSlot2ForArm9()) {
        SetActiveMemoryLayout(nullptr);
        WriteDetectSnapshot(state, false);
        g_debug_state.extra_ram_fail_stage = kExtraRamFailStageExpansionBusOwner;
        g_debug_state.detect_fail_reason = kDetectFailExpansionBusOwner;
        return false;
    }

    if (!UnlockOperaExpansionPak()) {
        SetActiveMemoryLayout(nullptr);
        WriteDetectSnapshot(state, false);
        g_debug_state.extra_ram_fail_stage = kExtraRamFailStageExpansionUnlock;
        g_debug_state.detect_fail_reason = kDetectFailExpansionUnlock;
        return false;
    }

    if (!InitializeExtraRamPool()) {
        SetActiveMemoryLayout(nullptr);
        WriteDetectSnapshot(state, false);
        g_debug_state.extra_ram_fail_stage = kExtraRamFailStageExpansionProbe;
        g_debug_state.detect_fail_reason = kDetectFailExpansionProbe;
        return false;
    }

    WriteDetectSnapshot(state, true);
    g_debug_state.detect_fail_reason = kDetectOk;
    g_debug_state.expansion_pak_detected = 1u;
    return true;
}

bool LoadOverlayImage(uint32_t *out_bytes_read) {
    if (g_active_memory_layout == nullptr || g_overlay_image_base == nullptr) {
        return false;
    }

    RuntimeFSFileID file_id = {};
    bool found = false;
    for (uint32_t i = 0; i < g_active_memory_layout->overlay_path_count; ++i) {
        const char *path = g_active_memory_layout->overlay_paths[i];
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

    const uint32_t overlay_max_image_size = GetOverlayMaxImageSize();
    FillBytes(g_overlay_image_base, 0xa5, overlay_max_image_size);
    const int bytes_read = FSReadFile(&file, g_overlay_image_base, overlay_max_image_size);
    FSCloseFile(&file);

    if (bytes_read <= 0) {
        return false;
    }

    *out_bytes_read = static_cast<uint32_t>(bytes_read);
    return true;
}

bool ValidateOverlayHeader(const ModOverlayHeader *header, uint32_t bytes_read) {
    const uint32_t overlay_max_image_size = GetOverlayMaxImageSize();
    const uint32_t overlay_max_bss_size = GetOverlayMaxBssSize();
    const uint32_t overlay_total_region_size = GetOverlayTotalRegionSize();

    if (header->magic != kModOverlayMagic) {
        return false;
    }
    if (header->abi_version != kModOverlayAbiVersion) {
        return false;
    }
    if (header->header_size < sizeof(ModOverlayHeader)) {
        return false;
    }
    if (header->image_size < header->header_size || header->image_size > overlay_max_image_size) {
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
    if (header->bss_size > overlay_max_bss_size) {
        return false;
    }
    const uint32_t total_size = header->image_size + header->bss_size;
    if (total_size < header->image_size || total_size > overlay_total_region_size) {
        return false;
    }

    const uint32_t header_crc =
        ComputeCrc32(header, static_cast<uint32_t>(offsetof(ModOverlayHeader, header_crc32)));
    if (header_crc != header->header_crc32) {
        return false;
    }

    const uint32_t payload_size = header->image_size - header->header_size;
    const uint32_t image_crc =
        ComputeCrc32(g_overlay_image_base + header->header_size, payload_size);
    if (image_crc != header->image_crc32) {
        return false;
    }

    return true;
}

uint32_t Host_GetRuntimeMode() {
    return static_cast<uint32_t>(g_mod_runtime_mode);
}

uint32_t Host_GetMemoryTier() {
    return static_cast<uint32_t>(g_mod_runtime_memory_tier);
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
    if (ModRuntime_IsEnhancedMemoryModeEnabled()) {
        g_host_api.host_flags |= kModOverlayHostFlagEnhancedMemory;
    }
    if (ModRuntime_IsExpansionPakModeEnabled()) {
        g_host_api.host_flags |= kModOverlayHostFlagExpansionPak;
    }
    if (ModRuntime_IsDsiModeEnabled()) {
        g_host_api.host_flags |= kModOverlayHostFlagDsi;
    }
    g_host_api.crc32 = ComputeCrc32;
    g_host_api.spawn_original = Host_SpawnOriginal;
    g_host_api.get_runtime_mode = Host_GetRuntimeMode;
    g_host_api.get_memory_tier = Host_GetMemoryTier;
    g_host_api.is_dsi_mode_enabled = Host_IsDsiModeEnabled;
    g_host_api.reserved[0] =
        (g_active_memory_layout != nullptr) ? static_cast<uint32_t>(g_active_memory_layout->overlay_heap_base_addr) : 0u;
    g_host_api.reserved[1] =
        (g_active_memory_layout != nullptr)
            ? static_cast<uint32_t>(g_active_memory_layout->overlay_guard_base_addr -
                                    g_active_memory_layout->overlay_heap_base_addr)
            : 0u;
    g_host_api.reserved[2] = static_cast<uint32_t>(g_mod_runtime_memory_tier);
}

bool TryLoadAndStartOverlay() {
    if (g_active_memory_layout == nullptr || g_overlay_image_base == nullptr) {
        g_debug_state.overlay_skip_reason = kOverlaySkipNoEnhancedMemory;
        return false;
    }

    g_debug_state.overlay_skip_reason = kOverlaySkipNone;
    g_debug_state.overlay_load_attempts++;
    uint32_t bytes_read = 0;
    if (!LoadOverlayImage(&bytes_read)) {
        g_debug_state.overlay_skip_reason = kOverlaySkipFileUnavailable;
        g_debug_state.overlay_load_failures++;
        return false;
    }

    const auto *header = reinterpret_cast<const ModOverlayHeader *>(g_overlay_image_base);
    if (!ValidateOverlayHeader(header, bytes_read)) {
        g_debug_state.overlay_skip_reason = kOverlaySkipHeaderInvalid;
        g_debug_state.overlay_load_failures++;
        return false;
    }

    ZeroBytes(g_overlay_image_base + header->image_size, header->bss_size);

    const uint32_t total_size = header->image_size + header->bss_size;
    DCFlushRange(g_overlay_image_base, static_cast<int>(total_size));
    DCWaitWriteBufferEmpty();
    ICInvalidateRange(g_overlay_image_base, static_cast<int>(header->image_size));

    uintptr_t entry_addr = g_active_memory_layout->overlay_image_base_addr + (header->entry_offset & ~1u);
    entry_addr |= (header->entry_offset & 1u);
    auto entry_fn = reinterpret_cast<ModOverlayEntryFn>(entry_addr);

    g_overlay_exports = reinterpret_cast<ModOverlayExports *>(g_overlay_image_base + header->exports_offset);
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

ModRuntimeMemoryTier ModRuntime_GetMemoryTier() {
    return g_mod_runtime_memory_tier;
}

bool ModRuntime_IsDsiModeEnabled() {
    return g_mod_runtime_mode == ModRuntimeMode::DSI_MODE_NO_OVERLAY ||
           g_mod_runtime_mode == ModRuntimeMode::DSI_MODE_OVERLAY_READY;
}

bool ModRuntime_IsExpansionPakModeEnabled() {
    return g_mod_runtime_mode == ModRuntimeMode::EXPANSION_MODE_NO_OVERLAY ||
           g_mod_runtime_mode == ModRuntimeMode::EXPANSION_MODE_OVERLAY_READY;
}

bool ModRuntime_IsEnhancedMemoryModeEnabled() {
    return ModRuntime_IsDsiModeEnabled() || ModRuntime_IsExpansionPakModeEnabled();
}

bool ModRuntime_IsOverlayReady() {
    return g_mod_runtime_mode == ModRuntimeMode::DSI_MODE_OVERLAY_READY ||
           g_mod_runtime_mode == ModRuntimeMode::EXPANSION_MODE_OVERLAY_READY;
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

uintptr_t ModRuntime_GetExtraRamBase() {
    return g_extra_ram_pool_base_addr;
}

uint32_t ModRuntime_GetExtraRamSize() {
    return g_extra_ram_pool_size;
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

void *ModRuntime_TryResizeExtraRamTail(void *ptr, uint32_t new_size) {
    if (!g_extra_ram_pool_ready || ptr == nullptr || new_size == 0u) {
        return nullptr;
    }

    const uintptr_t base = g_extra_ram_pool_base_addr;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    if (addr < base || addr >= (base + g_extra_ram_pool_size)) {
        return nullptr;
    }

    const uint32_t offset = static_cast<uint32_t>(addr - base);
    if (offset > g_extra_ram_pool_cursor) {
        return nullptr;
    }

    const uint32_t current_size = g_extra_ram_pool_cursor - offset;
    if (new_size <= current_size) {
        return ptr;
    }

    if (offset > g_extra_ram_pool_size || new_size > (g_extra_ram_pool_size - offset)) {
        g_debug_state.extram_alloc_failures++;
        return nullptr;
    }

    auto *tail = reinterpret_cast<uint8_t *>(ptr) + current_size;
    ZeroBytes(tail, new_size - current_size);

    g_extra_ram_pool_cursor = offset + new_size;
    g_debug_state.extram_pool_cursor = g_extra_ram_pool_cursor;
    g_debug_state.extram_last_alloc_addr = static_cast<uint32_t>(addr);
    g_debug_state.extram_last_alloc_size = new_size;
    g_debug_state.extram_last_alloc_alignment = 4u;
    return ptr;
}

bool ModRuntime_TryPromoteLoadedFile(uint32_t ext_file_id, const void *source_data, uint32_t source_size,
                                     ModRuntimeExtraRamFileLoadResult *out_result) {
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

bool ModRuntime_TryLoadFileToExtraRam(uint32_t ext_file_id, ModRuntimeExtraRamFilePolicy policy,
                                      ModRuntimeExtraRamFileLoadResult *out_result) {
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
            ModRuntimeExtraRamFileLoadResult promoted = {};
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

void ModRuntime_ExtraRamReset(uint16_t scene_id) {
    ClearCacheTableForReset(scene_id);
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
    g_debug_state.last_scene_change_id = scene_id;

    // Area-switch hooks can fire while old area assets are still live.
    // In practice, area-enter is still too early for some live animation state,
    // so defer the reset until overlay unload.
    if (scene_id == kModRuntimeSceneEventSwitchArea ||
        scene_id == kModRuntimeSceneEventAreaLeave) {
        if (!g_extra_ram_reset_pending) {
            Log() << "[DSI-MEM][LIFECYCLE] deferred reset pending scene="
                  << Log::Hex << scene_id << Log::Dec << "\n";
        }
        g_extra_ram_reset_pending = true;
    }

    const bool safe_reset_boundary =
        scene_id == kModRuntimeSceneEventOverlayUnload;
    if (safe_reset_boundary && g_extra_ram_pool_ready &&
        (g_extra_ram_reset_pending || scene_id == kModRuntimeSceneEventOverlayUnload)) {
        Log() << "[DSI-MEM][LIFECYCLE] applying deferred reset scene="
              << Log::Hex << scene_id << Log::Dec << "\n";
        ModRuntime_ExtraRamReset(scene_id);
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

void *ModRuntime_TryReservePreBootstrapExtraRam(uint32_t size, uint32_t alignment) {
    if (g_bootstrap_finished || size == 0u) {
        return nullptr;
    }

    return ReserveExpansionPakTop(size, alignment);
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
        g_debug_state.overlay_skip_reason = kOverlaySkipNoEnhancedMemory;
        g_mod_runtime_mode = ModRuntimeMode::NDS_MODE;
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
        if (g_mod_runtime_memory_tier == ModRuntimeMemoryTier::DSI) {
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
        } else {
            LogTwlGateSnapshot("FAIL");
            Log() << "[MODRUNTIME] Expansion Pak detect OK"
                  << " EXMEMCNT=" << Log::Hex << g_debug_state.raw_exmemcnt
                  << " UNLOCK=" << g_debug_state.raw_slot2_unlock << Log::Dec
                  << " POOL_BASE=" << Log::Hex << g_extra_ram_pool_base_addr
                  << " POOL_SIZE=" << Log::Dec << g_extra_ram_pool_size
                  << "\n";
        }
    }

    if (g_mod_runtime_memory_tier == ModRuntimeMemoryTier::DSI) {
        g_debug_state.dsi_detected_boots++;
        g_mod_runtime_mode = ModRuntimeMode::DSI_MODE_NO_OVERLAY;
    } else {
        g_debug_state.expansion_detected_boots++;
        g_mod_runtime_mode = ModRuntimeMode::EXPANSION_MODE_NO_OVERLAY;
    }
    g_debug_state.overlay_skip_reason = kOverlaySkipNone;
    if (g_extra_ram_pool_ready || InitializeExtraRamPool()) {
        Log() << "[MODRUNTIME] Extra RAM pool ready base=" << Log::Hex
              << g_extra_ram_pool_base_addr << " size=" << g_extra_ram_pool_size
              << " first_alloc_addr=deferred" << Log::Dec << "\n";
    } else {
        Log() << "[MODRUNTIME][WARNING] Extra RAM probe failed at base=" << Log::Hex
              << g_extra_ram_pool_base_addr << " expected=" << kExtraRamProbePattern
              << " got=" << g_debug_state.extram_probe_readback << Log::Dec << "\n";
    }

    SetupHostApi();
    if (g_mod_runtime_memory_tier == ModRuntimeMemoryTier::EXPANSION_PAK &&
        !EnableExpansionOverlayExecution()) {
        g_debug_state.overlay_skip_reason = kOverlaySkipEntryFailed;
        Log() << "[MODRUNTIME][OVERLAY] Expansion overlay execution permissions unavailable;"
                 " staying in memory-only mode.\n";
        g_debug_state.last_mode_snapshot = g_mod_runtime_mode;
        return;
    }
    if (TryLoadAndStartOverlay()) {
        g_mod_runtime_mode = (g_mod_runtime_memory_tier == ModRuntimeMemoryTier::DSI)
                                 ? ModRuntimeMode::DSI_MODE_OVERLAY_READY
                                 : ModRuntimeMode::EXPANSION_MODE_OVERLAY_READY;
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
