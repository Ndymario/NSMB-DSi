#ifdef NTR_DEBUG

#include "nsmb.hpp"

#include "mod_runtime.hpp"

namespace {

constexpr u32 kNoLoadingExtFileId = 0xffffffffu;
constexpr u32 kPreloadToDsiMinSize = 0xC000u;
constexpr u32 kPromoteAfterLoadMinSize = 0x2000u;
constexpr uintptr_t kExtraRamPoolBaseAddr = 0x02c10000u;
constexpr uintptr_t kExtraRamPoolEndAddr = 0x02ffffffu;
u32 g_loading_ext_file_id = kNoLoadingExtFileId;
bool g_low_heap_logged = false;
bool g_setup3d_in_progress = false;
u32 g_setup3d_file_id = kNoLoadingExtFileId;
void *g_setup3d_file_data = nullptr;

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

void LogDsiPoolState(const char *tag) {
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
    auto *dst = reinterpret_cast<u8 *>(dest);
    auto *source = reinterpret_cast<const u8 *>(src);
    for (u32 i = 0; i < size; ++i) {
        dst[i] = source[i];
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

bool IsExtraRamPointer(const void *ptr) {
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    return addr >= kExtraRamPoolBaseAddr && addr <= kExtraRamPoolEndAddr;
}

}  // namespace

asm(R"(
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
            LogDsiPoolState("resize-tail-fail");
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
        !compressed &&
        file_size >= kPreloadToDsiMinSize) {
        if (void *dsi_data = LoadRawFileToExtraRam(extFileID, file_size)) {
            self->size = file_size;
            self->fileID = scast<u16>(extFileID & 0xFFFFu);
            self->compressed = compressed;
            self->data = dsi_data;
            self->heap = nullptr;

            Log() << "[DSI-MEM][FS] preload-loaded"
                  << " realID=" << ToRealFileId(extFileID)
                  << " size=" << file_size << " to_dsi=1\n";
            LogDsiPoolState("preload-load");
            g_loading_ext_file_id = kNoLoadingExtFileId;
            return dsi_data;
        }
    }

    void *data = FS_Cache_CacheEntry_loadFile_SUPER(self, extFileID, compressed);
    if (data == nullptr) {
        Log() << "[DSI-MEM][FS] loadFile FAILED realID=" << ToRealFileId(extFileID)
              << " size=" << file_size << " free_heap=" << free_heap << "\n";
        LogDsiPoolState("fs-load-fail");

        if (!compressed && file_size >= kPromoteAfterLoadMinSize) {
            if (void *dsi_data = LoadRawFileToExtraRam(extFileID, file_size)) {
                self->size = file_size;
                self->fileID = scast<u16>(extFileID & 0xFFFFu);
                self->compressed = compressed;
                self->data = dsi_data;
                self->heap = nullptr;

                Log() << "[DSI-MEM][FS] fallback-loaded"
                      << " realID=" << ToRealFileId(extFileID)
                      << " size=" << file_size << " to_dsi=1\n";
                LogDsiPoolState("fallback-load");
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
        !compressed &&
        self->data != nullptr &&
        loaded_size >= kPromoteAfterLoadMinSize) {
        if (void *promoted_data = CopyRawFileToExtraRam(self->data, loaded_size)) {
            Heap *const original_heap = self->heap;
            void *const original_data = self->data;

            self->data = promoted_data;
            self->size = loaded_size;
            self->heap = nullptr;
            data = promoted_data;

            original_heap->deallocate(original_data);

            Log() << "[DSI-MEM][FS] promote-loaded"
                  << " realID=" << ToRealFileId(extFileID)
                  << " size=" << loaded_size << " to_dsi=1\n";
            LogDsiPoolState("promote-load");
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
        LogDsiPoolState("overlay-load-fail");
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
    g_setup3d_file_id = g_loading_ext_file_id;
    g_setup3d_file_data = pResData;

    Log() << "[DSI-MEM][3D] setup start"
          << " realID=" << ToRealFileId(g_setup3d_file_id)
          << " data=" << Log::Hex << reinterpret_cast<uintptr_t>(pResData)
          << Log::Dec << "\n";

    const BOOL result = NNSG3dResDefaultSetupOriginal(pResData);

    Log() << "[DSI-MEM][3D] setup done"
          << " realID=" << ToRealFileId(g_setup3d_file_id)
          << " result=" << static_cast<u32>(result) << "\n";

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
    void *ptr = Heap_allocate_SUPER(self, size, align);

    if (ptr == nullptr && (self->flags & 0x4000u) != 0u) {
        const u32 free_now = self->vMaxAllocatableSize(align);
        Log() << "[DSI-MEM][HEAP] OOM size=" << size
              << " align=" << align
              << " free_now=" << free_now
              << " file_real_id=" << ToRealFileId(g_loading_ext_file_id)
              << "\n";
        LogDsiPoolState("heap-oom");
    } else if (self == Memory::currentHeapPtr && !g_low_heap_logged) {
        const u32 free_now = self->vMaxAllocatableSize(4);
        if (free_now < 0x8000u) {
            g_low_heap_logged = true;
            Log() << "[DSI-MEM][HEAP] low memory warning free_now=" << free_now << "\n";
            LogDsiPoolState("heap-low");
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
