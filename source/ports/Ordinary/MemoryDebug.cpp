#ifdef NTR_DEBUG

#include "nsmb.hpp"

#include "mod_runtime.hpp"

namespace {

constexpr u32 kNoLoadingExtFileId = 0xffffffffu;
constexpr u32 kPreloadToExtraRamMinSizeDsi = 0xC000u;
constexpr u32 kPreloadToExtraRamMinSizeExpansion = 0x4000u;
constexpr u32 kPreloadCompressedToExtraRamMinSizeExpansion = 0x400u;
constexpr u32 kPromoteAfterLoadMinSizeDsi = 0x2000u;
constexpr u32 kPromoteAfterLoadMinSizeExpansion = 0x0400u;
constexpr u32 kMagicNsbmd = 0x30444D42u;
constexpr u32 kMagicNsbtx = 0x30585442u;
constexpr u32 kMaxExtraRamResources = 96u;
constexpr u32 kHeapTraceHistorySize = 16u;
u32 g_loading_ext_file_id = kNoLoadingExtFileId;
bool g_low_heap_logged = false;
bool g_setup3d_in_progress = false;
u32 g_setup3d_file_id = kNoLoadingExtFileId;
void *g_setup3d_file_data = nullptr;
uintptr_t g_last_heap_alloc_caller = 0u;
u32 g_last_heap_alloc_size = 0u;
u32 g_last_heap_alloc_align = 0u;
u32 g_last_heap_alloc_file_id = kNoLoadingExtFileId;
u32 g_last_heap_alloc_free_before = 0u;
u32 g_last_heap_alloc_free_after = 0u;
void *g_last_heap_alloc_ptr = nullptr;

struct HeapTraceEntry {
    uintptr_t caller;
    void *ptr;
    u32 size;
    u32 align;
    u32 free_before;
    u32 free_after;
    u32 real_file_id;
};

HeapTraceEntry g_heap_trace_history[kHeapTraceHistorySize] = {};
u32 g_heap_trace_cursor = 0u;

struct ExtraRamResourceEntry {
    void *data;
    u32 size;
    u32 real_file_id;
    bool setup_complete;
};

ExtraRamResourceEntry g_extra_ram_resources[kMaxExtraRamResources] = {};

u32 ToRealFileId(u32 ext_file_id) {
    if (ext_file_id == kNoLoadingExtFileId) {
        return 0xffffffffu;
    }
    return (ext_file_id & 0xFFFFu) + 131u;
}

bool ShouldKeepOnMainHeap(u32 ext_file_id) {
    const u32 real_file_id = ToRealFileId(ext_file_id);

    // The particle/spl*.spa family is not safe to bounce through the DSi pool across
    // scene transitions and reloads. We observed crashes with:
    // - 1860: particle/spl.spa
    // - 1861: particle/spl_b01_kpa.spa
    // - 1870: particle/spl_coursesel.spa
    // Keep the full contiguous SPA block on the original heap until the consumer
    // lifetime / ownership path is understood.
    return real_file_id >= 1859u && real_file_id <= 1871u;
}

bool ShouldPreloadCompressedToExtraRam(u32 ext_file_id, u32 compressed_size) {
    if (!ModRuntime_IsExpansionPakModeEnabled() ||
        compressed_size < kPreloadCompressedToExtraRamMinSizeExpansion) {
        return false;
    }

    const u32 real_file_id = ToRealFileId(ext_file_id);

    // Title-screen boot pressure is dominated by the decompressed window banner and
    // player model family. Decode those directly into Expansion RAM instead of
    // parking their final buffers in the vanilla heap.
    return real_file_id == 1687u || (real_file_id >= 1873u && real_file_id <= 1896u);
}

u32 GetPreloadMinSize() {
    return ModRuntime_IsExpansionPakModeEnabled() ? kPreloadToExtraRamMinSizeExpansion
                                                  : kPreloadToExtraRamMinSizeDsi;
}

u32 GetPromoteAfterLoadMinSize() {
    return ModRuntime_IsExpansionPakModeEnabled() ? kPromoteAfterLoadMinSizeExpansion
                                                  : kPromoteAfterLoadMinSizeDsi;
}

bool ShouldPromoteAfterLoad(bool compressed, u32 loaded_size) {
    if (loaded_size < GetPromoteAfterLoadMinSize()) {
        return false;
    }

    // The cache entry bookkeeping is only known-safe for post-load promotion when the
    // original path already treated the payload as an ordinary heap-owned buffer.
    // Compressed entries are still managed by extra loader state after load, and
    // redirecting them into extra RAM currently leaves the cache lifecycle inconsistent.
    return !compressed;
}

void LogExtraRamPoolState(const char *tag) {
    const ModRuntimeDebugState *state = ModRuntime_GetDebugState();
    if (state == nullptr) {
        return;
    }

    Log() << "[DSI-MEM][" << tag << "] mode=" << static_cast<u32>(ModRuntime_GetMode())
          << " pool_base=" << Log::Hex << state->extram_pool_base
          << " pool_size=" << state->extram_pool_size
          << " cursor=" << state->extram_pool_cursor
          << " alloc_ok=" << Log::Dec << state->extram_alloc_successes
          << " alloc_fail=" << state->extram_alloc_failures
          << " promoted_files=" << ModRuntime_GetPromotedFileCount()
          << " last_size=" << state->extram_last_alloc_size
          << " last_addr=" << Log::Hex << state->extram_last_alloc_addr << Log::Dec
          << "\n";
}

void CopyBytes(void *dest, const void *src, u32 size) {
    const uintptr_t dest_addr = reinterpret_cast<uintptr_t>(dest);
    const uintptr_t extra_base = ModRuntime_GetExtraRamBase();
    const uint32_t extra_size = ModRuntime_GetExtraRamSize();
    const bool is_extra_ram_dest =
        extra_size != 0u && dest_addr >= extra_base && dest_addr < (extra_base + extra_size);

    auto *dst8 = reinterpret_cast<u8 *>(dest);
    auto *source8 = reinterpret_cast<const u8 *>(src);
    if (!is_extra_ram_dest) {
        for (u32 i = 0; i < size; ++i) {
            dst8[i] = source8[i];
        }
        return;
    }

    auto *dst32 = reinterpret_cast<u32 *>(dest);
    auto *source32 = reinterpret_cast<const u32 *>(src);
    u32 copied = 0u;

    while ((copied + 4u) <= size) {
        *dst32++ = *source32++;
        copied += 4u;
    }

    if ((size - copied) >= 2u) {
        *reinterpret_cast<volatile u16 *>(reinterpret_cast<uintptr_t>(dst32)) =
            *reinterpret_cast<const u16 *>(source8 + copied);
        copied += 2u;
    }

    if (copied < size) {
        const u16 tail = source8[copied];
        *reinterpret_cast<volatile u16 *>(reinterpret_cast<uintptr_t>(dst8 + copied)) = tail;
    }
}

void *LoadRawFileToExtraRam(u32 extFileID, u32 file_size) {
    if (!ModRuntime_IsExtraRamPoolReady() || file_size == 0u) {
        return nullptr;
    }

    void *dest = ModRuntime_ExtraRamAlloc(file_size, 32);
    if (dest == nullptr) {
        return nullptr;
    }

    const s32 read = FS::loadExtFile(extFileID, dest, static_cast<s32>(file_size));
    return read == static_cast<s32>(file_size) ? dest : nullptr;
}

void *CopyRawFileToExtraRam(const void *source, u32 file_size) {
    if (!ModRuntime_IsExtraRamPoolReady() || source == nullptr || file_size == 0u) {
        return nullptr;
    }

    void *dest = ModRuntime_ExtraRamAlloc(file_size, 32);
    if (dest == nullptr) {
        return nullptr;
    }

    CopyBytes(dest, source, file_size);
    return dest;
}

ExtraRamResourceEntry *FindExtraRamResourceByPointer(const void *ptr) {
    if (ptr == nullptr) {
        return nullptr;
    }

    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    for (u32 i = 0; i < kMaxExtraRamResources; ++i) {
        ExtraRamResourceEntry &entry = g_extra_ram_resources[i];
        if (entry.data == nullptr || entry.size == 0u) {
            continue;
        }

        const uintptr_t base = reinterpret_cast<uintptr_t>(entry.data);
        if (addr >= base && addr < (base + entry.size)) {
            return &entry;
        }
    }
    return nullptr;
}

void RememberExtraRamResource(void *data, u32 size, u32 ext_file_id) {
    if (data == nullptr || size == 0u) {
        return;
    }

    if (ExtraRamResourceEntry *existing = FindExtraRamResourceByPointer(data)) {
        existing->data = data;
        existing->size = size;
        existing->real_file_id = ToRealFileId(ext_file_id);
        return;
    }

    for (u32 i = 0; i < kMaxExtraRamResources; ++i) {
        ExtraRamResourceEntry &entry = g_extra_ram_resources[i];
        if (entry.data == nullptr) {
            entry.data = data;
            entry.size = size;
            entry.real_file_id = ToRealFileId(ext_file_id);
            entry.setup_complete = false;
            return;
        }
    }
}

void DumpRecentHeapTrace(const char *tag) {
    Log() << "[DSI-MEM][HEAP] recent-trace tag=" << tag << "\n";
    for (u32 i = 0; i < kHeapTraceHistorySize; ++i) {
        const u32 index = (g_heap_trace_cursor + i) % kHeapTraceHistorySize;
        const HeapTraceEntry &entry = g_heap_trace_history[index];
        if (entry.caller == 0u && entry.ptr == nullptr && entry.size == 0u) {
            continue;
        }

        Log() << "[DSI-MEM][HEAP] trace"
              << " size=" << entry.size
              << " align=" << entry.align
              << " free_before=" << entry.free_before
              << " free_after=" << entry.free_after
              << " ptr=" << Log::Hex << reinterpret_cast<uintptr_t>(entry.ptr)
              << " caller=" << entry.caller
              << Log::Dec << " file_real_id=" << entry.real_file_id << "\n";
    }
}

u32 ReadResourceMagic(const void *data) {
    return (data != nullptr) ? *reinterpret_cast<const u32 *>(data) : 0u;
}

using AllocFromCurrentHeapFn = void *(*)(int size, int align);
using AllocFromCurrentHeapFromTopFn = void *(*)(u32 size);
using FreeToCurrentHeapFn = void (*)(void *ptr);
using InitNnsSoundFn = void (*)(void *heap, u32 size);
using StartSoundCaptureEffectFn = void (*)(void *buffer, u32 size);
using SimpleVoidFn = void (*)();
using MiUncompressLZ16Fn = void (*)(const void *source, void *dest);
using DCStoreRangeFn = void (*)(const void *start, int size);
using DCWaitWriteBufferEmptyFn = void (*)();

static AllocFromCurrentHeapFn AllocFromCurrentHeapOriginal =
    reinterpret_cast<AllocFromCurrentHeapFn>(0x02044a8c);
static AllocFromCurrentHeapFromTopFn AllocFromCurrentHeapFromTopOriginal =
    reinterpret_cast<AllocFromCurrentHeapFromTopFn>(0x02044a6c);
static FreeToCurrentHeapFn FreeToCurrentHeapOriginal =
    reinterpret_cast<FreeToCurrentHeapFn>(0x02044a50);
static InitNnsSoundFn InitNnsSoundOriginal =
    reinterpret_cast<InitNnsSoundFn>(0x0204f2e0);
static StartSoundCaptureEffectFn StartSoundCaptureEffectOriginal =
    reinterpret_cast<StartSoundCaptureEffectFn>(0x0204f1dc);
static SimpleVoidFn InitSoundOriginal =
    reinterpret_cast<SimpleVoidFn>(0x0204f2d4);
static SimpleVoidFn MoreMusicStuffOriginal =
    reinterpret_cast<SimpleVoidFn>(0x02012714);
static MiUncompressLZ16Fn MiUncompressLZ16Original =
    reinterpret_cast<MiUncompressLZ16Fn>(0x02067258);
static DCStoreRangeFn DCStoreRangeOriginal =
    reinterpret_cast<DCStoreRangeFn>(0x02065ccc);
static DCWaitWriteBufferEmptyFn DCWaitWriteBufferEmptyOriginal =
    reinterpret_cast<DCWaitWriteBufferEmptyFn>(0x02065d0c);

u32 GetLzDecompressedSize(const void *source) {
    if (source == nullptr) {
        return 0u;
    }
    return (*reinterpret_cast<const u32 *>(source)) >> 8;
}

void StoreLoadedFileDataCache(void *data, u32 size) {
    if (data == nullptr || size == 0u) {
        return;
    }

    DCStoreRangeOriginal(data, static_cast<int>(size));
    DCWaitWriteBufferEmptyOriginal();
}

void *LoadLzFileToExtraRam(u32 extFileID, u32 compressed_size, u32 *out_loaded_size) {
    if (out_loaded_size == nullptr || compressed_size == 0u ||
        !ModRuntime_IsExtraRamPoolReady() || Memory::currentHeapPtr == nullptr) {
        return nullptr;
    }

    *out_loaded_size = 0u;

    void *compressed_data = AllocFromCurrentHeapOriginal(static_cast<int>(compressed_size), -4);
    if (compressed_data == nullptr) {
        return nullptr;
    }

    const s32 read = FS::loadExtFile(extFileID, compressed_data, static_cast<s32>(compressed_size));
    if (read != static_cast<s32>(compressed_size)) {
        FreeToCurrentHeapOriginal(compressed_data);
        return nullptr;
    }

    const u32 decompressed_size = GetLzDecompressedSize(compressed_data);
    if (decompressed_size == 0u) {
        FreeToCurrentHeapOriginal(compressed_data);
        return nullptr;
    }

    void *dest = ModRuntime_ExtraRamAlloc(decompressed_size, 32);
    if (dest == nullptr) {
        FreeToCurrentHeapOriginal(compressed_data);
        return nullptr;
    }

    MiUncompressLZ16Original(compressed_data, dest);
    FreeToCurrentHeapOriginal(compressed_data);
    StoreLoadedFileDataCache(dest, decompressed_size);

    *out_loaded_size = decompressed_size;
    return dest;
}

bool IsExtraRamPointer(const void *ptr) {
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t base = ModRuntime_GetExtraRamBase();
    const uint32_t size = ModRuntime_GetExtraRamSize();
    return size != 0u && addr >= base && addr < (base + size);
}

uintptr_t ReadLinkRegister() {
    uintptr_t lr = 0u;
    asm volatile("mov %0, lr" : "=r"(lr));
    return lr;
}

constexpr u32 kSoundHeapSize = 0xC0000u;
constexpr u32 kSoundCaptureBufferSize = 0x1000u;

void InitSoundNoDlpWithExtraRamFallback() {
    void *sound_heap = AllocFromCurrentHeapFromTopOriginal(kSoundHeapSize);
    InitNnsSoundOriginal(sound_heap, kSoundHeapSize);

    void *buffer = AllocFromCurrentHeapOriginal(kSoundCaptureBufferSize, 0x20);
    StartSoundCaptureEffectOriginal(buffer, kSoundCaptureBufferSize);
    InitSoundOriginal();
    MoreMusicStuffOriginal();
}

}  // namespace

asm(R"(
.type LoadFileInto3D_SUPER, %function
LoadFileInto3D_SUPER:
    PUSH    {R4,R5,LR}
    B       0x02009df0

.type FS_Cache_CacheEntry_loadFile_SUPER, %function
FS_Cache_CacheEntry_loadFile_SUPER:
    PUSH    {R4-R6,LR}
    B       0x0200A238

.type FS_Cache_CacheEntry_loadFileToOverlay_SUPER, %function
FS_Cache_CacheEntry_loadFileToOverlay_SUPER:
    PUSH    {R4-R6,LR}
    B       0x0200A198

.type Heap_allocate_SUPER, %function
Heap_allocate_SUPER:
    PUSH    {R4-R7,LR}
    B       0x02045044

.type Heap_deallocate_SUPER, %function
Heap_deallocate_SUPER:
    PUSH    {R4,R5,LR}
    B       0x02044D98
)");

extern "C" {
BOOL LoadFileInto3D_SUPER(void *file, int trim_tex);
void *FS_Cache_CacheEntry_loadFile_SUPER(FS::Cache::CacheEntry *self, u32 extFileID, bool compressed);
void *FS_Cache_CacheEntry_loadFileToOverlay_SUPER(FS::Cache::CacheEntry *self, u32 extFileID,
                                                   bool compressed);
void *Heap_allocate_SUPER(Heap *self, u32 size, int align);
void Heap_deallocate_SUPER(Heap *self, void *ptr);
}

using ResizeHeapFn = void *(*)(Heap *heap, void *ptr, u32 newSize);
static ResizeHeapFn ResizeHeapOriginal = reinterpret_cast<ResizeHeapFn>(0x02044af8);
using NNSG3dResDefaultSetupFn = BOOL (*)(void *pResData);
static NNSG3dResDefaultSetupFn NNSG3dResDefaultSetupOriginal =
    reinterpret_cast<NNSG3dResDefaultSetupFn>(0x02059b68);
using ModelLoadFn = void *(*)(void *file, u32 model_id, void **texture);
static ModelLoadFn ModelLoadOriginal = reinterpret_cast<ModelLoadFn>(0x02019b7c);

bool FinalizeExtraRam3DResource(void *data, u32 size, u32 ext_file_id) {
    RememberExtraRamResource(data, size, ext_file_id);

    const u32 magic = ReadResourceMagic(data);
    if (magic != kMagicNsbmd && magic != kMagicNsbtx) {
        return true;
    }

    const u32 saved_loading_id = g_loading_ext_file_id;
    g_loading_ext_file_id = ext_file_id;
    const BOOL result = LoadFileInto3D_SUPER(data, 1);
    g_loading_ext_file_id = saved_loading_id;

    if (!result) {
        Log() << "[DSI-MEM][3D] preload setup FAILED"
              << " realID=" << ToRealFileId(ext_file_id)
              << " data=" << Log::Hex << reinterpret_cast<uintptr_t>(data)
              << Log::Dec << "\n";
        return false;
    }

    if (ExtraRamResourceEntry *entry = FindExtraRamResourceByPointer(data)) {
        entry->setup_complete = true;
    }
    StoreLoadedFileDataCache(data, size);
    return true;
}

ncp_jump(0x020125e8)
void InitSoundNoDlp_OVERRIDE() {
    InitSoundNoDlpWithExtraRamFallback();
}

ncp_call(0x02019964)
void *ModelCreate_LoadModel_Hook(void *file, u32 model_id, void **texture) {
    const u32 magic = ReadResourceMagic(file);
    const ExtraRamResourceEntry *entry = FindExtraRamResourceByPointer(file);

    Log() << "[DSI-MEM][MODEL] load request"
          << " file=" << Log::Hex << reinterpret_cast<uintptr_t>(file)
          << " model_id=" << Log::Dec << model_id
          << " magic=" << Log::Hex << magic << Log::Dec;
    if (entry != nullptr) {
        Log() << " realID=" << entry->real_file_id
              << " setup=" << static_cast<u32>(entry->setup_complete);
    }
    Log() << "\n";

    return ModelLoadOriginal(file, model_id, texture);
}

ncp_jump(0x02009dec)
BOOL LoadFileInto3D_OVERRIDE(void *file, int trim_tex) {
    if (IsExtraRamPointer(file)) {
        if (ExtraRamResourceEntry *entry = FindExtraRamResourceByPointer(file)) {
            if (entry->setup_complete) {
                return 1;
            }
        }
    }

    const BOOL result = LoadFileInto3D_SUPER(file, trim_tex);
    if (result && IsExtraRamPointer(file)) {
        if (ExtraRamResourceEntry *entry = FindExtraRamResourceByPointer(file)) {
            entry->setup_complete = true;
        }
    }
    return result;
}

void *Setup3DFile_ResizeHeapShim(Heap *heap, void *ptr, u32 newSize) {
    if (IsExtraRamPointer(ptr)) {
        void *resized = ModRuntime_TryResizeExtraRamTail(ptr, newSize);
        if (g_setup3d_in_progress && resized != nullptr) {
            Log() << "[DSI-MEM][3D] resize-tail"
                  << " realID=" << ToRealFileId(g_setup3d_file_id)
                  << " file=" << Log::Hex << reinterpret_cast<uintptr_t>(g_setup3d_file_data)
                  << " ptr=" << reinterpret_cast<uintptr_t>(ptr)
                  << Log::Dec << " new_size=" << newSize << "\n";
        }
        if (resized == nullptr) {
            Log() << "[DSI-MEM][FS] resize-tail FAILED"
                  << " ptr=" << Log::Hex << reinterpret_cast<uintptr_t>(ptr)
                  << " new_size=" << Log::Dec << newSize << "\n";
            LogExtraRamPoolState("resize-tail-fail");
        }
        return resized;
    }
    return ResizeHeapOriginal(heap, ptr, newSize);
}

ncp_jump(0x0200A234)
void *FS_Cache_CacheEntry_loadFile_OVERRIDE(FS::Cache::CacheEntry *self, u32 extFileID, bool compressed) {
    g_loading_ext_file_id = extFileID;

    const u32 file_size = FS::getFileSize(extFileID);
    const u32 free_heap =
        (Memory::currentHeapPtr != nullptr) ? Memory::currentHeapPtr->vMaxAllocatableSize(4) : 0;

    if (ModRuntime_IsExtraRamPoolReady() &&
        !ShouldKeepOnMainHeap(extFileID) &&
        compressed &&
        ShouldPreloadCompressedToExtraRam(extFileID, file_size)) {
        u32 loaded_size = 0u;
        if (void *dsi_data = LoadLzFileToExtraRam(extFileID, file_size, &loaded_size)) {
            if (!FinalizeExtraRam3DResource(dsi_data, loaded_size, extFileID)) {
                g_loading_ext_file_id = kNoLoadingExtFileId;
                return nullptr;
            }
            self->size = loaded_size;
            self->fileID = scast<u16>(extFileID & 0xFFFFu);
            self->compressed = compressed;
            self->data = dsi_data;
            self->heap = nullptr;

            Log() << "[DSI-MEM][FS] preload-lz"
                  << " realID=" << ToRealFileId(extFileID)
                  << " compressed_size=" << file_size
                  << " loaded_size=" << loaded_size
                  << " to_dsi=1\n";
            LogExtraRamPoolState("preload-lz");
            g_loading_ext_file_id = kNoLoadingExtFileId;
            return dsi_data;
        }
    }

    if (ModRuntime_IsExtraRamPoolReady() &&
        !ShouldKeepOnMainHeap(extFileID) &&
        !compressed &&
        file_size >= GetPreloadMinSize()) {
        if (void *dsi_data = LoadRawFileToExtraRam(extFileID, file_size)) {
            if (!FinalizeExtraRam3DResource(dsi_data, file_size, extFileID)) {
                g_loading_ext_file_id = kNoLoadingExtFileId;
                return nullptr;
            }
            self->size = file_size;
            self->fileID = scast<u16>(extFileID & 0xFFFFu);
            self->compressed = compressed;
            self->data = dsi_data;
            self->heap = nullptr;

            Log() << "[DSI-MEM][FS] preload-loaded"
                  << " realID=" << ToRealFileId(extFileID)
                  << " size=" << file_size << " to_dsi=1\n";
            LogExtraRamPoolState("preload-load");
            g_loading_ext_file_id = kNoLoadingExtFileId;
            return dsi_data;
        }
    }

    void *data = FS_Cache_CacheEntry_loadFile_SUPER(self, extFileID, compressed);
    if (data == nullptr) {
        Log() << "[DSI-MEM][FS] loadFile FAILED realID=" << ToRealFileId(extFileID)
              << " size=" << file_size << " free_heap=" << free_heap << "\n";
        LogExtraRamPoolState("fs-load-fail");

        if (!compressed && file_size >= GetPromoteAfterLoadMinSize()) {
            if (void *dsi_data = LoadRawFileToExtraRam(extFileID, file_size)) {
                if (!FinalizeExtraRam3DResource(dsi_data, file_size, extFileID)) {
                    g_loading_ext_file_id = kNoLoadingExtFileId;
                    return nullptr;
                }
                self->size = file_size;
                self->fileID = scast<u16>(extFileID & 0xFFFFu);
                self->compressed = compressed;
                self->data = dsi_data;
                self->heap = nullptr;

                Log() << "[DSI-MEM][FS] fallback-loaded"
                      << " realID=" << ToRealFileId(extFileID)
                      << " size=" << file_size << " to_dsi=1\n";
                LogExtraRamPoolState("fallback-load");
                g_loading_ext_file_id = kNoLoadingExtFileId;
                return dsi_data;
            }
        }
        g_loading_ext_file_id = kNoLoadingExtFileId;
        return nullptr;
    }

    const u32 loaded_size = self->size;
    if (ModRuntime_IsExtraRamPoolReady() &&
        !ShouldKeepOnMainHeap(extFileID) &&
        self->heap != nullptr &&
        self->data != nullptr &&
        ShouldPromoteAfterLoad(compressed, loaded_size)) {
        if (void *promoted_data = CopyRawFileToExtraRam(self->data, loaded_size)) {
            if (!FinalizeExtraRam3DResource(promoted_data, loaded_size, extFileID)) {
                Log() << "[DSI-MEM][FS] promote finalize FAILED"
                      << " realID=" << ToRealFileId(extFileID) << "\n";
                g_loading_ext_file_id = kNoLoadingExtFileId;
                return data;
            }
            Heap *const original_heap = self->heap;
            void *const original_data = self->data;

            self->data = promoted_data;
            self->size = loaded_size;
            self->heap = nullptr;
            data = promoted_data;

            original_heap->deallocate(original_data);

            Log() << "[DSI-MEM][FS] promote-loaded"
                  << " realID=" << ToRealFileId(extFileID)
                  << " size=" << loaded_size
                  << " compressed=" << static_cast<u32>(compressed)
                  << " tier=" << static_cast<u32>(ModRuntime_GetMemoryTier())
                  << "\n";
            LogExtraRamPoolState("promote-load");
        }
    }

    const u32 free_heap_after =
        (Memory::currentHeapPtr != nullptr) ? Memory::currentHeapPtr->vMaxAllocatableSize(4) : 0;

    Log() << "[DSI-MEM][FS] loadFile realID=" << ToRealFileId(extFileID)
          << " loaded_size=" << loaded_size
          << " free_heap_now=" << free_heap_after << "\n";
    g_loading_ext_file_id = kNoLoadingExtFileId;
    return data;
}

ncp_jump(0x0200A194)
void *FS_Cache_CacheEntry_loadFileToOverlay_OVERRIDE(FS::Cache::CacheEntry *self, u32 extFileID,
                                                      bool compressed) {
    g_loading_ext_file_id = extFileID;

    const u32 file_size = FS::getFileSize(extFileID);
    const u32 required_size = (file_size + 15u) & 0xFFFFFFF0u;

    void *data = FS_Cache_CacheEntry_loadFileToOverlay_SUPER(self, extFileID, compressed);
    if (data == nullptr) {
        Log() << "[DSI-MEM][FS] loadFileToOverlay FAILED realID=" << ToRealFileId(extFileID)
              << " req_size=" << required_size
              << " overlay_free=" << FS::Cache::overlayFileSize << "\n";
        LogExtraRamPoolState("overlay-load-fail");
        g_loading_ext_file_id = kNoLoadingExtFileId;
        return nullptr;
    }

    Log() << "[DSI-MEM][FS] loadFileToOverlay realID=" << ToRealFileId(extFileID)
          << " req_size=" << required_size
          << " overlay_free_now=" << FS::Cache::overlayFileSize << "\n";
    g_loading_ext_file_id = kNoLoadingExtFileId;
    return data;
}

ncp_call(0x02009E1C)
BOOL Setup3DFile_NNSG3dResDefaultSetup_Hook(void *pResData) {
    g_setup3d_in_progress = true;
    if (g_loading_ext_file_id != kNoLoadingExtFileId) {
        g_setup3d_file_id = g_loading_ext_file_id;
    } else if (ExtraRamResourceEntry *entry = FindExtraRamResourceByPointer(pResData)) {
        g_setup3d_file_id = (entry->real_file_id >= 131u) ? (entry->real_file_id - 131u) : kNoLoadingExtFileId;
    } else {
        g_setup3d_file_id = kNoLoadingExtFileId;
    }
    g_setup3d_file_data = pResData;

    Log() << "[DSI-MEM][3D] setup start"
          << " realID=" << ToRealFileId(g_setup3d_file_id)
          << " data=" << Log::Hex << reinterpret_cast<uintptr_t>(pResData)
          << Log::Dec << "\n";

    const BOOL result = NNSG3dResDefaultSetupOriginal(pResData);

    Log() << "[DSI-MEM][3D] setup done"
          << " realID=" << ToRealFileId(g_setup3d_file_id)
          << " result=" << static_cast<u32>(result) << "\n";

    if (result && IsExtraRamPointer(pResData)) {
        if (ExtraRamResourceEntry *entry = FindExtraRamResourceByPointer(pResData)) {
            entry->setup_complete = true;
        }
    }

    g_setup3d_in_progress = false;
    g_setup3d_file_id = kNoLoadingExtFileId;
    g_setup3d_file_data = nullptr;
    return result;
}

ncp_call(0x02009E60)
void *Setup3DFile_ResizeHeapShim_Hook(Heap *heap, void *ptr, u32 newSize) {
    return Setup3DFile_ResizeHeapShim(heap, ptr, newSize);
}

ncp_repl(0x020450D4, "NOP")

ncp_jump(0x02045040)
void *Heap_allocate_OVERRIDE(Heap *self, u32 size, int align) {
    const bool is_current_heap = self == Memory::currentHeapPtr;
    const u32 free_before = is_current_heap ? self->vMaxAllocatableSize(4) : 0u;
    const uintptr_t caller = ReadLinkRegister();
    void *ptr = Heap_allocate_SUPER(self, size, align);
    const u32 free_after = is_current_heap ? self->vMaxAllocatableSize(4) : 0u;

    if (is_current_heap) {
        g_last_heap_alloc_caller = caller;
        g_last_heap_alloc_size = size;
        g_last_heap_alloc_align = static_cast<u32>(align);
        g_last_heap_alloc_file_id = g_loading_ext_file_id;
        g_last_heap_alloc_free_before = free_before;
        g_last_heap_alloc_free_after = free_after;
        g_last_heap_alloc_ptr = ptr;

        HeapTraceEntry &trace = g_heap_trace_history[g_heap_trace_cursor % kHeapTraceHistorySize];
        trace.caller = caller;
        trace.ptr = ptr;
        trace.size = size;
        trace.align = static_cast<u32>(align);
        trace.free_before = free_before;
        trace.free_after = free_after;
        trace.real_file_id = ToRealFileId(g_loading_ext_file_id);
        g_heap_trace_cursor = (g_heap_trace_cursor + 1u) % kHeapTraceHistorySize;

        if ((ptr != nullptr && size >= 0x20000u) ||
            (free_before >= 0x20000u && free_after < 0x20000u)) {
            Log() << "[DSI-MEM][HEAP] alloc-trace"
                  << " size=" << size
                  << " align=" << align
                  << " free_before=" << free_before
                  << " free_after=" << free_after
                  << " ptr=" << Log::Hex << reinterpret_cast<uintptr_t>(ptr)
                  << " caller=" << caller
                  << Log::Dec << " file_real_id=" << ToRealFileId(g_loading_ext_file_id)
                  << "\n";
        }
    }

    if (ptr == nullptr && (self->flags & 0x4000u) != 0u) {
        const u32 free_now = self->vMaxAllocatableSize(align);
        Log() << "[DSI-MEM][HEAP] OOM size=" << size
              << " align=" << align
              << " free_now=" << free_now
              << " caller=" << Log::Hex << caller << Log::Dec
              << " file_real_id=" << ToRealFileId(g_loading_ext_file_id)
              << "\n";
        DumpRecentHeapTrace("oom");
        LogExtraRamPoolState("heap-oom");
    } else if (self == Memory::currentHeapPtr && !g_low_heap_logged) {
        const u32 free_now = self->vMaxAllocatableSize(4);
        if (free_now < 0x8000u) {
            g_low_heap_logged = true;
            Log() << "[DSI-MEM][HEAP] low memory warning free_now=" << free_now
                  << " last_size=" << g_last_heap_alloc_size
                  << " last_align=" << g_last_heap_alloc_align
                  << " last_free_before=" << g_last_heap_alloc_free_before
                  << " last_free_after=" << g_last_heap_alloc_free_after
                  << " last_ptr=" << Log::Hex << reinterpret_cast<uintptr_t>(g_last_heap_alloc_ptr)
                  << " last_caller=" << g_last_heap_alloc_caller
                  << Log::Dec << " last_file_real_id="
                  << ToRealFileId(g_last_heap_alloc_file_id) << "\n";
            DumpRecentHeapTrace("low");
            LogExtraRamPoolState("heap-low");
        }
    }

    return ptr;
}

ncp_jump(0x02044D94)
void Heap_deallocate_OVERRIDE(Heap *self, void *ptr) {
    Heap_deallocate_SUPER(self, ptr);

    if (self == Memory::currentHeapPtr) {
        const u32 free_now = self->vMaxAllocatableSize(4);
        if (free_now >= 0x10000u) {
            g_low_heap_logged = false;
        }
    }
}

#endif
