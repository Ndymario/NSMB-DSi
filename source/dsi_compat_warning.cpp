#include "nsmb.hpp"
#include "mod_runtime.hpp"

namespace {

bool g_logged_boot_warning = false;
bool g_logged_boot_hook = false;

// Ghidra-validated callsites in BootScene__onExecute:
//   0x02186AA0 (overlay 1 -> in-game 0x020CC720)
//   0x02186E50 (overlay 1 -> in-game 0x020CCAD0)
void BootScene_SwitchSceneWarningHook() {
    ModRuntime_NotifySceneChange(kModRuntimeSceneEventBootSceneSwitch);

    if (!g_logged_boot_hook) {
        g_logged_boot_hook = true;
        Log() << "[MODRUNTIME][BOOTSCENE] switchScene callsite reached mode="
              << static_cast<u32>(ModRuntime_GetMode()) << "\n";
    }
    if (ModRuntime_IsDsiCompatModeLikely() && !g_logged_boot_warning) {
        g_logged_boot_warning = true;
        Log() << "[MODRUNTIME][WARNING] BootScene is continuing in NDS compatibility mode on DSi "
                 "hardware; custom DSi RAM overlay features are inactive. Use a true TWL launch "
                 "path for DSi RAM features.\n";
    }

}

}  // namespace

ncp_hook(0x020CCAD0, 1)
void BootScene_SwitchSceneWarningHookA() {
    BootScene_SwitchSceneWarningHook();
}

ncp_hook(0x020CC720, 1)
void BootScene_SwitchSceneWarningHookB() {
    BootScene_SwitchSceneWarningHook();
}
