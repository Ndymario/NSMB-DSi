/*
The "chaser" is the sp00ky Luigi that the player can see chasing them when sp00ky mode is activated.
*/

#include "SpookyChaser.hpp"
#include "SpookyController.hpp"
#include "mod_runtime.hpp"

namespace {

static constexpr u32 kNsbtxMagic = 0x30585442; // "BTX0"
void* s_chaser_tex_l = nullptr;
void* s_chaser_tex_m = nullptr;
u32 s_chaser_tex_l_ext = 0;
u32 s_chaser_tex_m_ext = 0;
u32 s_chaser_tex_generation = 0;

}  // namespace

ncp_over(0x020C619C, 0) const ObjectInfo objectInfo = Chaser::objectInfo; //Stage Actor ID 192
ncp_over(0x02039AEC) static constexpr const ActorProfile* profile = &Chaser::profile; //objectID 92

bool Chaser::onPrepareResources(){
    const u32 generation = ModRuntime_GetExtraRamGeneration();
    if (s_chaser_tex_generation != generation) {
        s_chaser_tex_l = nullptr;
        s_chaser_tex_m = nullptr;
        s_chaser_tex_l_ext = 0;
        s_chaser_tex_m_ext = 0;
        s_chaser_tex_generation = generation;
    }

    const char* texPath = nullptr;
    void** cachedFile = nullptr;
    u32* cachedExt = nullptr;
    if(!Game::getPlayerCharacter(currentTarget)){
        texPath = "/z_new/Ordinary/actors/Chaser_L.nsbtx";
        cachedFile = &s_chaser_tex_l;
        cachedExt = &s_chaser_tex_l_ext;
    } else {
        texPath = "/z_new/Ordinary/actors/Chaser_M.nsbtx";
        cachedFile = &s_chaser_tex_m;
        cachedExt = &s_chaser_tex_m_ext;
    }

    if (*cachedFile != nullptr) {
        texID = 0;
        spookyNsbtx.setup(*cachedFile, Vec2(64, 64), Vec2(0, 0), 0, 0);
        textureGeneration = generation;
        gfxReady = true;
        Log() << "[ORDINARY][Chaser] Reused cached texture path=" << texPath
              << " extFileId=" << *cachedExt << "\n";
        return true;
    }

    u32 extFileId = 0;
    void* nsbtxFile = nullptr;
    if (!ModRuntime_LoadValidatedFile(texPath, kNsbtxMagic, &extFileId, &nsbtxFile)) {
        gfxReady = false;
        Log() << "[ORDINARY][Chaser] Failed to resolve/load texture with BTX0 magic path="
              << texPath << "\n";
        return false;
    }

    *cachedFile = nsbtxFile;
    *cachedExt = extFileId;
    Log() << "[ORDINARY][Chaser] Loaded texture path=" << texPath << " extFileId=" << extFileId << "\n";
    texID = 0;
	spookyNsbtx.setup(nsbtxFile, Vec2(64, 64), Vec2(0, 0), 0, 0);
    textureGeneration = generation;
    gfxReady = true;
    return 1;
}

bool Chaser::loadResources() {
    return true;
}

// Code that runs the first time the Actor loads in
s32 Chaser::onCreate() {
	ctrl = SpookyController::getInstance();

    onPrepareResources();
	loadResources();

    // Reload palette to restore chaser colors (palette was made greyscale before chaser spawned)
    reloadPalette();

    viewOffset = Vec2(50, 50);
    activeSize = Vec2(1000.0, 3000.0);

    resetMusic = true;

    return 1;
}

// Code that runs every frame
bool Chaser::updateMain() {
    const u32 generation = ModRuntime_GetExtraRamGeneration();
    if (textureGeneration != generation) {
        Log() << "[ORDINARY][Chaser] Rebinding texture after DSi pool generation change old="
              << textureGeneration << " new=" << generation << "\n";
        gfxReady = false;
        onPrepareResources();
        if (gfxReady) {
            reloadPalette();
        }
    }

    if (gfxReady) {
        spookyNsbtx.setTexture(texID);
        spookyNsbtx.setPalette(texID);
    }
    if(ctrl->deathTimer % 5 == 0){
        texID = (texID + 1) % 6;
    }
	moveTowardsPlayer();

    ctrl->deathTimer -= 1;

    if (ctrl->deathTimer <= 0) {
        targetPlayer->damage(*this, 0, 0, PlayerDamageType::Death);
        if(Game::vsMode){
            ctrl->onBlockHit();
        }
    }

    return 1;
}

void Chaser::moveTowardsPlayer() {
    targetPlayer = Game::getPlayer(currentTarget);

    if (ctrl->deathTimer >= ctrl->suspenseTime) {
        position.x = targetPlayer->position.x - ctrl->deathTimer * 1.0fx;
    } else {
        position.x = targetPlayer->position.x - playerBuffer - ctrl->deathTimer * 0.25fx;
    }

    position.y = targetPlayer->position.y - 16fx;
    position.z = targetPlayer->position.z;

    wrapPosition(position);
    
    if(resetMusic){
        if(!Game::vsMode){
            SND::pauseBGM(true);
        }
        SND::playSFXUnique(380);
        resetMusic = false;
    }
}

s32 Chaser::onRender() {
    if (!gfxReady) {
        return 1;
    }
    Vec3 scale(1fx);
    if(ctrl->deathTimer < ctrl->suspenseTime){
        spookyNsbtx.render(position, scale);
    }
    return 1;
}

// Code runs when this Actor is being destroyed
s32 Chaser::onDestroy() {
    SpookyController* controller = SpookyController::getInstance();
	controller->chaser = nullptr;
    return 1;
}
