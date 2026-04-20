#pragma once

#include <cstdint>

#include "mod_overlay_api.hpp"

enum class ModRuntimeMode : uint32_t {
    UNINITIALIZED = 0,
    NDS_MODE = 1,
    EXPANSION_MODE_NO_OVERLAY = 2,
    EXPANSION_MODE_OVERLAY_READY = 3,
    DSI_MODE_NO_OVERLAY = 4,
    DSI_MODE_OVERLAY_READY = 5,
};

enum class ModRuntimeMemoryTier : uint32_t {
    NONE = 0,
    EXPANSION_PAK = 1,
    DSI = 2,
};

enum class ModRuntimeExtraRamFilePolicy : uint32_t {
    PROMOTE_ALWAYS = 0,
    PROMOTE_IF_LARGE = 1,
};
using ModRuntimeDsiFilePolicy = ModRuntimeExtraRamFilePolicy;

struct ModRuntimeExtraRamFileLoadResult {
    void *data;
    uint32_t size;
    uint32_t magic;
    bool reused;
};
using ModRuntimeDsiFileLoadResult = ModRuntimeExtraRamFileLoadResult;

struct ModRuntimeDebugState {
    uint32_t bootstrap_calls;
    uint32_t bootstrap_seen;
    uint32_t dsi_detected_boots;
    uint32_t expansion_detected_boots;
    uint32_t nds_fallback_boots;
    uint32_t strict_twl_passed;
    uint32_t strict_twl_failed;
    uint32_t extra_ram_fail_stage;
    uint32_t overlay_skip_reason;
    uint32_t overlay_load_attempts;
    uint32_t overlay_load_successes;
    uint32_t overlay_load_failures;

    uint32_t spawn_dispatch_calls;
    uint32_t spawn_custom_id_attempts;
    uint32_t overlay_spawn_attempts;
    uint32_t overlay_spawn_successes;
    uint32_t fallback_spawn_calls;

    uint32_t detect_fail_reason;
    uint32_t raw_scfg_a9rom;
    uint32_t raw_scfg_ext9;
    uint32_t raw_scfg_ext9_ram_limit;
    uint32_t raw_scfg_a9rom_before_unlock;
    uint32_t raw_scfg_ext9_before_unlock;
    uint32_t raw_scfg_a9rom_after_unlock;
    uint32_t raw_scfg_ext9_after_unlock;
    uint32_t raw_scfg_ext9_ram_limit_after_unlock;
    uint32_t raw_exmemcnt;
    uint32_t raw_sndexcnt;
    uint32_t raw_slot2_unlock;
    uint32_t dsi_console_hint;
    uint32_t dsi_hw_likely;
    uint32_t dsi_compat_mode_likely;
    uint32_t dsi_compat_warning_emitted;
    uint32_t expansion_pak_detected;
    // Deprecated compat-unlock diagnostics kept for ABI stability; always zero in strict TWL mode.
    uint32_t compat_unlock_candidate;
    uint32_t compat_unlock_attempts;
    uint32_t compat_unlock_successes;
    uint32_t compat_unlock_failures;
    uint32_t compat_unlock_active;

    uint32_t mpu_mainram_before;
    uint32_t mpu_mainram_after;
    uint32_t mpu_mainram_expected;
    uint32_t mpu_update_attempts;
    uint32_t mpu_update_successes;
    uint32_t mpu_update_failures;
    uint32_t compat_preserve_attempts;
    uint32_t compat_preserve_successes;
    uint32_t compat_preserve_failures;

    uint32_t extram_probe_attempts;
    uint32_t extram_probe_successes;
    uint32_t extram_probe_failures;
    uint32_t extram_probe_addr;
    uint32_t extram_probe_pattern;
    uint32_t extram_probe_readback;
    uint32_t extram_pool_base;
    uint32_t extram_pool_size;
    uint32_t extram_pool_cursor;
    uint32_t extram_alloc_calls;
    uint32_t extram_alloc_successes;
    uint32_t extram_alloc_failures;
    uint32_t extram_last_alloc_size;
    uint32_t extram_last_alloc_alignment;
    uint32_t extram_last_alloc_addr;
    uint32_t extram_pool_memory_tier;
    uint32_t overlay_image_base;
    uint32_t overlay_image_size;

    uint16_t last_object_id;
    uint16_t last_custom_object_id;
    uint16_t last_scene_change_id;
    uint16_t scene_change_reserved;
    uint32_t last_settings;
    uint8_t last_base_type;
    uint8_t reserved[3];
    uint32_t scene_change_calls;
    uint32_t overlay_scene_callbacks;

    ModRuntimeMode last_mode_snapshot;
};

ModRuntimeMemoryTier ModRuntime_GetMemoryTier();
bool ModRuntime_IsDsiModeEnabled();
bool ModRuntime_IsExpansionPakModeEnabled();
bool ModRuntime_IsEnhancedMemoryModeEnabled();
bool ModRuntime_IsOverlayReady();
bool ModRuntime_IsDsiCompatModeLikely();
bool ModRuntime_IsCompatUnlockEnabled();
bool ModRuntime_IsExtraRamPoolReady();
uintptr_t ModRuntime_GetExtraRamBase();
uint32_t ModRuntime_GetExtraRamSize();
void *ModRuntime_ExtraRamAlloc(uint32_t size, uint32_t alignment = 32);
void *ModRuntime_TryResizeExtraRamTail(void *ptr, uint32_t new_size);
bool ModRuntime_TryLoadFileToExtraRam(uint32_t ext_file_id, ModRuntimeExtraRamFilePolicy policy,
                                      ModRuntimeExtraRamFileLoadResult *out_result);
bool ModRuntime_TryPromoteLoadedFile(uint32_t ext_file_id, const void *source_data, uint32_t source_size,
                                     ModRuntimeExtraRamFileLoadResult *out_result);
bool ModRuntime_LoadValidatedFile(const char *path, uint32_t expected_magic, uint32_t *out_ext_file_id,
                                  void **out_file);
void ModRuntime_ExtraRamReset(uint16_t scene_id);
uint32_t ModRuntime_GetExtraRamGeneration();
uint32_t ModRuntime_GetPromotedFileCount();
void ModRuntime_NotifySceneChange(uint16_t scene_id);
ModRuntimeMode ModRuntime_GetMode();
const ModRuntimeDebugState *ModRuntime_GetDebugState();
const char *ModRuntime_GetDetectReasonLabel(uint32_t code);
const char *ModRuntime_GetOverlaySkipReasonLabel(uint32_t code);
bool ModRuntime_TryLateCompatEnable();
void *ModRuntime_TryReservePreBootstrapExtraRam(uint32_t size, uint32_t alignment);

constexpr uint16_t kModRuntimeSceneEventSwitchArea = 0xfffe;
constexpr uint16_t kModRuntimeSceneEventBootSceneSwitch = 0xfffd;
constexpr uint16_t kModRuntimeSceneEventAreaEnter = 0xfffc;
constexpr uint16_t kModRuntimeSceneEventAreaLeave = 0xfffb;
constexpr uint16_t kModRuntimeSceneEventOverlayUnload = 0xfffa;

template <typename TDsi, typename TNds>
inline auto ModRuntime_IfDsi(TDsi dsi_value, TNds nds_value) -> decltype(dsi_value) {
    return ModRuntime_IsDsiModeEnabled() ? dsi_value : nds_value;
}

template <typename TEnhanced, typename TNds>
inline auto ModRuntime_IfEnhancedMemory(TEnhanced enhanced_value,
                                        TNds nds_value) -> decltype(enhanced_value) {
    return ModRuntime_IsEnhancedMemoryModeEnabled() ? enhanced_value : nds_value;
}

#define MODRUNTIME_IF_DSI(dsi_expr, nds_expr) \
    (ModRuntime_IsDsiModeEnabled() ? (dsi_expr) : (nds_expr))

#define MODRUNTIME_IF_ENHANCED_MEMORY(enhanced_expr, nds_expr) \
    (ModRuntime_IsEnhancedMemoryModeEnabled() ? (enhanced_expr) : (nds_expr))

extern "C" void ModRuntime_BootstrapHook();
extern "C" void *ModRuntime_SpawnDispatch(uint16_t object_id, void *parent_node, uint32_t settings,
                                          uint8_t base_type);
