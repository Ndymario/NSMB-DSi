#ifdef NTR_DEBUG

#include "nsmb.hpp"

#include "mod_runtime.hpp"

namespace {

u32 g_loading_ext_file_id = 0;
bool g_low_heap_logged = false;

u32 ToRealFileId(u32 ext_file_id) {
    return (ext_file_id & 0xFFFFu) + 131u;
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

ncp_jump(0x0200A234)
void *FS_Cache_CacheEntry_loadFile_OVERRIDE(FS::Cache::CacheEntry *self, u32 extFileID, bool compressed) {
    g_loading_ext_file_id = extFileID;

    const u32 file_size = FS::getFileSize(extFileID);
    const u32 free_heap =
        (Memory::currentHeapPtr != nullptr) ? Memory::currentHeapPtr->vMaxAllocatableSize(4) : 0;

    void *data = FS_Cache_CacheEntry_loadFile_SUPER(self, extFileID, compressed);
    if (data == nullptr) {
        Log() << "[DSI-MEM][FS] loadFile FAILED realID=" << ToRealFileId(extFileID)
              << " size=" << file_size << " free_heap=" << free_heap << "\n";
        LogDsiPoolState("fs-load-fail");

        ModRuntimeDsiFileLoadResult dsi_load = {};
        if (ModRuntime_TryLoadFileToExtraRam(extFileID, ModRuntimeDsiFilePolicy::PROMOTE_IF_LARGE,
                                             &dsi_load)) {
            self->size = dsi_load.size;
            self->fileID = scast<u16>(extFileID & 0xFFFFu);
            self->compressed = compressed;
            self->data = dsi_load.data;
            self->heap = nullptr;

            Log() << "[DSI-MEM][FS] fallback-" << (dsi_load.reused ? "reused" : "loaded")
                  << " realID=" << ToRealFileId(extFileID)
                  << " size=" << dsi_load.size << " to_dsi=1\n";
            LogDsiPoolState("fallback-load");
            return dsi_load.data;
        }
        return nullptr;
    }

    const u32 loaded_size = self->size;
    const u32 free_heap_after =
        (Memory::currentHeapPtr != nullptr) ? Memory::currentHeapPtr->vMaxAllocatableSize(4) : 0;

    Log() << "[DSI-MEM][FS] loadFile realID=" << ToRealFileId(extFileID)
          << " loaded_size=" << loaded_size
          << " free_heap_now=" << free_heap_after << "\n";
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
        return nullptr;
    }

    Log() << "[DSI-MEM][FS] loadFileToOverlay realID=" << ToRealFileId(extFileID)
          << " req_size=" << required_size
          << " overlay_free_now=" << FS::Cache::overlayFileSize << "\n";
    return data;
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
