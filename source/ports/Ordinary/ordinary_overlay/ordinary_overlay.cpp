#include "mod_overlay_api.hpp"
#include "ordinary_spawn_mappings.hpp"

namespace {

const ModOverlayHostApi* g_host_api = nullptr;

struct SpookyControllerState {
    bool active = false;
    uint16_t target_player = 0;
    uint16_t last_scene_id = 0;
    uint32_t scene_change_count = 0;
};

SpookyControllerState g_spooky_controller = {};

extern "C" void* OrdinaryOverlay_SpawnCustom(uint16_t object_id,
                                             void* parent_node,
                                             uint32_t settings,
                                             uint8_t base_type) {
    if (g_host_api == nullptr || g_host_api->spawn_original == nullptr) {
        return nullptr;
    }

    using namespace ordinary_overlay;
    switch (object_id) {
        case kCustomSpookyController: {
            g_spooky_controller.active = true;
            g_spooky_controller.target_player = static_cast<uint16_t>(settings & 0x1u);
            // Ordinary's SpookyController drives Chaser actor 92; spawn it here as the translated
            // overlay-side controller entry point.
            return g_host_api->spawn_original(kOrdinarySpookyChaserActor, parent_node, settings,
                                              base_type);
        }
        case kCustomSpookyChaser:
            return g_host_api->spawn_original(kOrdinarySpookyChaserActor, parent_node, settings, base_type);
        default:
            return nullptr;
    }
}

extern "C" void OrdinaryOverlay_OnSceneChange(uint16_t scene_id) {
    g_spooky_controller.last_scene_id = scene_id;
    g_spooky_controller.scene_change_count++;

    // Runtime synthetic event for area transitions: reset controller state.
    if (scene_id == 0xfffeu) {
        g_spooky_controller.active = false;
    }
}

extern "C" void OrdinaryOverlay_Shutdown() {
    g_spooky_controller = {};
    g_host_api = nullptr;
}

}  // namespace

extern "C" {
ModOverlayExports g_ordinary_exports = {
    kModOverlayAbiVersion,
    0,
    OrdinaryOverlay_SpawnCustom,
    OrdinaryOverlay_OnSceneChange,
    OrdinaryOverlay_Shutdown,
    {0, 0, 0},
};
}

extern "C" bool OrdinaryOverlay_Entry(const ModOverlayHostApi* host_api) {
    if (host_api == nullptr) {
        return false;
    }
    if (host_api->abi_version != kModOverlayAbiVersion) {
        return false;
    }
    if (host_api->spawn_original == nullptr) {
        return false;
    }

    g_host_api = host_api;
    g_ordinary_exports.abi_version = kModOverlayAbiVersion;
    return true;
}
