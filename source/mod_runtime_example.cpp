#include "mod_runtime_example.hpp"

#include "mod_runtime.hpp"

namespace ModRuntimeExample {
namespace {

BranchCounters g_branch_counters = {};

bool IsCustomId(u16 object_id) {
    return object_id >= kCustomExampleObjectMin && object_id <= kCustomExampleObjectMax;
}

}  // namespace

u16 SelectObjectIdForCurrentMode(u16 vanilla_object_id, u16 custom_object_id) {
    g_branch_counters.last_requested_vanilla_id = vanilla_object_id;
    g_branch_counters.last_requested_custom_id = custom_object_id;

    // Custom IDs are only valid in 0x03xx.
    if (!IsCustomId(custom_object_id)) {
        g_branch_counters.nds_or_no_overlay_calls++;
        g_branch_counters.last_dispatched_id = vanilla_object_id;
        return vanilla_object_id;
    }

    // Only route custom actors when DSi mode is enabled and the overlay is actually ready.
    if (ModRuntime_IsDsiModeEnabled() && ModRuntime_IsOverlayReady()) {
        g_branch_counters.dsi_overlay_ready_calls++;
        g_branch_counters.last_dispatched_id = custom_object_id;
        return custom_object_id;
    }

    g_branch_counters.nds_or_no_overlay_calls++;
    g_branch_counters.last_dispatched_id = vanilla_object_id;
    return vanilla_object_id;
}

Base *SpawnChildWithDsiFallback(Base *parent, u32 settings, ObjectType type, u16 vanilla_object_id,
                                u16 custom_object_id) {
    g_branch_counters.spawn_child_calls++;
    const u16 selected = SelectObjectIdForCurrentMode(vanilla_object_id, custom_object_id);

    // This goes through Base::spawnChild, matching vanilla modder usage via nsmb.hpp.
    // The runtime's ncp_call patch at 0x0204c93c decides overlay-vs-vanilla spawning.
    return Base::spawnChild(selected, parent, settings, type);
}

const BranchCounters *GetBranchCounters() {
    return &g_branch_counters;
}

}  // namespace ModRuntimeExample
