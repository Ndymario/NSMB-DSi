#include "nsmb.hpp"
#include "mod_runtime.hpp"

// Redirect Bowser resource loads to reduce main RAM pressure in W1 Bowser.
// Verified in Ghidra:
//   FS::Cache::loadFile        @ 0x02009C64
//   FS::Cache::loadFileToOverlay @ 0x02009C14

asm(R"(
.type FS_Cache_loadFile_SUPER, %function
FS_Cache_loadFile_SUPER:
	stmdb sp!,{lr}
	b 0x02009C68

.type FS_Cache_loadFileToOverlay_SUPER, %function
FS_Cache_loadFileToOverlay_SUPER:
	stmdb sp!,{lr}
	b 0x02009C18
)");

extern "C" {
	void* FS_Cache_loadFile_SUPER(u32 extFileID, bool compressed);
	void* FS_Cache_loadFileToOverlay_SUPER(u32 extFileID, bool compressed);
}

// extFileID = fileID - 131
constexpr u32 kBowserAnimExt = 0x576;     // file ID 1529: enemy/koopa_new.nsbca
constexpr u32 kBowserModelExt = 0x577;    // file ID 1530: enemy/koopa_new.nsbmd
constexpr u32 kBoneKoopaMdlExt = 0x4D9;   // file ID 1372: enemy/bonekoopa.nsbmd
constexpr u32 kBoneKoopaAnmExt = 0x4DA;   // file ID 1373: enemy/bonekoopa2.nsbca

struct BowserOverlayFixDebugStats {
	u32 nds_passthrough_calls;
	u32 dsi_active_calls;
	u32 bowser_anim_redirect_calls;
	u32 dry_bowser_overlay_attempts;
	u32 dry_bowser_overlay_successes;
	u32 dry_bowser_ram_fallbacks;
	u32 to_overlay_ram_fallbacks;
	u32 last_ext_file_id;
	u32 last_required_size;
	u32 last_overlay_free;
};

static BowserOverlayFixDebugStats sDebugStats = {};
static bool sDryBowserCleanupDone = false;
static bool sLoggedNdsMode = false;
static bool sLoggedDsiMode = false;

static inline u32 align16(u32 size)
{
	return (size + 15) & 0xFFFFFFF0;
}

const BowserOverlayFixDebugStats* BowserOverlayFixDsi_GetDebugStats()
{
	return &sDebugStats;
}

static inline bool isDsiRuntimeActive()
{
	// Hard gate: never alter vanilla loading behavior unless DSi mode is confirmed.
	const bool active = ModRuntime_IsDsiModeEnabled();

	if (active) {
		sDebugStats.dsi_active_calls++;
		if (!sLoggedDsiMode) {
			Log() << "[DSI-PORT][BowserOverlayFix] DSi mode detected: DSi overlay routing enabled.\n";
			sLoggedDsiMode = true;
		}
	} else {
		sDebugStats.nds_passthrough_calls++;
		if (!sLoggedNdsMode) {
			Log() << "[DSI-PORT][BowserOverlayFix] NDS mode: using vanilla FS cache behavior.\n";
			sLoggedNdsMode = true;
		}
	}

	return active;
}

ncp_jump(0x02009C64)
void* FS_Cache_loadFile_OVERRIDE(u32 extFileID, bool compressed)
{
	if (!isDsiRuntimeActive()) {
		return FS_Cache_loadFile_SUPER(extFileID, compressed);
	}

	if (extFileID == kBowserAnimExt) {
		sDebugStats.bowser_anim_redirect_calls++;
		sDebugStats.last_ext_file_id = extFileID;
		// Move Bowser animation to overlay heap to save main RAM.
		Log() << "[DSI-PORT][BowserOverlayFix] Redirect Bowser anim to overlay extFileID=" << extFileID
		      << " freeOverlay=" << FS::Cache::overlayFileSize << "\n";
		return FS_Cache_loadFileToOverlay_SUPER(extFileID, compressed);
	}

	if (extFileID == kBoneKoopaMdlExt || extFileID == kBoneKoopaAnmExt) {
		// Dry Bowser assets: use overlay only when there is enough space.
		u32 required = align16(FS::getFileSize(extFileID));
		u32 freeOverlay = FS::Cache::overlayFileSize;
		u32 bowserAnimSize = align16(FS::getFileSize(kBowserAnimExt));
		bool canFit = (freeOverlay + bowserAnimSize) >= required;
		sDebugStats.dry_bowser_overlay_attempts++;
		sDebugStats.last_ext_file_id = extFileID;
		sDebugStats.last_required_size = required;
		sDebugStats.last_overlay_free = freeOverlay;

		if (canFit) {
			sDebugStats.dry_bowser_overlay_successes++;
			Log() << "[DSI-PORT][BowserOverlayFix] Dry Bowser overlay path extFileID=" << extFileID
			      << " required=" << required << " freeOverlay=" << freeOverlay << "\n";
			// Unload Bowser resources to free RAM/overlay headroom.
			if (!sDryBowserCleanupDone) {
				FS::Cache::unloadFile(kBowserAnimExt);
				FS::Cache::unloadFile(kBowserModelExt);
				sDryBowserCleanupDone = true;
			}

			return FS_Cache_loadFileToOverlay_SUPER(extFileID, compressed);
		}

		sDebugStats.dry_bowser_ram_fallbacks++;
		Log() << "[DSI-PORT][BowserOverlayFix] Dry Bowser RAM fallback extFileID=" << extFileID
		      << " required=" << required << " freeOverlay=" << freeOverlay
		      << " bowserAnimReserve=" << bowserAnimSize << "\n";
		// Fallback to RAM to avoid overlay OOM in W1 Bowser.
		return FS_Cache_loadFile_SUPER(extFileID, compressed);
	}

	return FS_Cache_loadFile_SUPER(extFileID, compressed);
}

ncp_jump(0x02009C14)
void* FS_Cache_loadFileToOverlay_OVERRIDE(u32 extFileID, bool compressed)
{
	if (!isDsiRuntimeActive()) {
		return FS_Cache_loadFileToOverlay_SUPER(extFileID, compressed);
	}

	if (extFileID == kBoneKoopaMdlExt || extFileID == kBoneKoopaAnmExt) {
		u32 required = align16(FS::getFileSize(extFileID));
		sDebugStats.last_ext_file_id = extFileID;
		sDebugStats.last_required_size = required;
		sDebugStats.last_overlay_free = FS::Cache::overlayFileSize;
		if (FS::Cache::overlayFileSize < required) {
			sDebugStats.to_overlay_ram_fallbacks++;
			Log() << "[DSI-PORT][BowserOverlayFix] loadFileToOverlay fallback extFileID=" << extFileID
			      << " required=" << required << " freeOverlay=" << FS::Cache::overlayFileSize << "\n";
			// Not enough overlay space; fall back to RAM.
			return FS_Cache_loadFile_SUPER(extFileID, compressed);
		}
	}

	return FS_Cache_loadFileToOverlay_SUPER(extFileID, compressed);
}
