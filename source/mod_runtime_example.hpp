#pragma once

#include "nsmb.hpp"

// Example helper API for mod code that wants clean enhanced-memory-vs-NDS branching.
namespace ModRuntimeExample {

constexpr u16 kCustomExampleObjectMin = 0x0300;
constexpr u16 kCustomExampleObjectMax = 0x03ff;
constexpr u16 kCustomExampleObjectId = 0x0300;

struct BranchCounters {
    u32 dsi_overlay_ready_calls;
    u32 nds_or_no_overlay_calls;
    u32 spawn_child_calls;
    u16 last_requested_vanilla_id;
    u16 last_requested_custom_id;
    u16 last_dispatched_id;
    u16 reserved;
};

u16 SelectObjectIdForCurrentMode(u16 vanilla_object_id, u16 custom_object_id = kCustomExampleObjectId);

Base *SpawnChildWithDsiFallback(Base *parent, u32 settings, ObjectType type, u16 vanilla_object_id,
                                u16 custom_object_id = kCustomExampleObjectId);

const BranchCounters *GetBranchCounters();

}  // namespace ModRuntimeExample
