#pragma once

#include <cstdint>

constexpr uint32_t kModOverlayMagic = 0x4D4F4456;      // "MODV"
constexpr uint16_t kModOverlayAbiVersion = 1;
constexpr uint32_t kModOverlayHostFlagEnhancedMemory = 1u << 0;
constexpr uint32_t kModOverlayHostFlagExpansionPak = 1u << 1;
constexpr uint32_t kModOverlayHostFlagDsi = 1u << 2;

using ModOverlaySpawnCustomFn = void *(*)(uint16_t object_id, void *parent_node, uint32_t settings,
                                          uint8_t base_type);
using ModOverlaySceneChangeFn = void (*)(uint16_t scene_id);
using ModOverlayShutdownFn = void (*)();

struct ModOverlayHostApi {
    uint32_t abi_version;
    uint32_t host_flags;
    uint32_t (*crc32)(const void *data, uint32_t size);
    ModOverlaySpawnCustomFn spawn_original;
    uint32_t (*get_runtime_mode)();
    uint32_t (*get_memory_tier)();
    uint32_t (*is_dsi_mode_enabled)();
    uint32_t reserved[3];
};

struct ModOverlayExports {
    uint32_t abi_version;
    uint32_t flags;
    ModOverlaySpawnCustomFn spawn_custom;
    ModOverlaySceneChangeFn on_scene_change;
    ModOverlayShutdownFn shutdown;
    uint32_t reserved[3];
};

struct ModOverlayHeader {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t entry_offset;
    uint32_t exports_offset;
    uint32_t image_crc32;
    uint32_t header_crc32;
};

using ModOverlayEntryFn = bool (*)(const ModOverlayHostApi *host_api);
