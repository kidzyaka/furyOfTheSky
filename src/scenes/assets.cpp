#include "scenes/scene.h"

#include <cstdio>
#include <cstring>

namespace scenes {

bool Assets::load() {
    struct TextureFile {
        gfx::Texture* texture;
        const char* path;
    };
    const TextureFile textures[] = {
        {&liveries[0], "assets/models/su35s_vks.ftx"},
        {&liveries[1], "assets/models/su35s_901.ftx"},
        {&r73Skin, "assets/models/r73_std.ftx"},
        {&r77Skin, "assets/models/r77_std.ftx"},
        {&shipSkin, "assets/models/patrol_ship_std.ftx"},
        {&seaDetail, "assets/textures/sea_detail.ftx"},
        {&seaFoam, "assets/textures/sea_foam.ftx"},
        {&deck, "assets/textures/deck.ftx"},
        {&deckBase, "assets/textures/deck_base.ftx"},
        {&deckStorm, "assets/textures/deck_storm.ftx"},
        {&deckScud, "assets/textures/deck_scud.ftx"},
        {&cloud, "assets/textures/cloud.ftx"},
        {&sun, "assets/textures/sun.ftx"},
        {&glow, "assets/textures/fx_glow.ftx"},
        {&smoke, "assets/textures/fx_smoke.ftx"},
        {&flame, "assets/textures/fx_flame.ftx"},
        {&ring, "assets/textures/fx_ring.ftx"},
        {&glint, "assets/textures/fx_glint.ftx"},
        {&shadow, "assets/textures/fx_shadow.ftx"},
        {&line, "assets/textures/fx_line.ftx"},
        {&ribbon, "assets/textures/fx_ribbon.ftx"},
    };
    struct MeshFile {
        gfx::Mesh* mesh;
        const char* path;
    };
    const MeshFile meshes[] = {
        {&su35s, "assets/models/su35s.fsm"},
        {&su35sLod, "assets/models/su35s_lod.fsm"},
        {&r73, "assets/models/r73.fsm"},
        {&r77, "assets/models/r77.fsm"},
        {&ship, "assets/models/patrol_ship.fsm"},
        {&shipLod, "assets/models/patrol_ship_lod.fsm"},
    };
    for (const auto& m : meshes) {
        if (!m.mesh->load(m.path)) {
            error = m.path;
            return false;
        }
    }
    for (const auto& t : textures) {
        if (!t.texture->load(t.path)) {
            error = t.path;
            return false;
        }
    }
    if (!loadRocks()) return false;
    struct SoundFile {
        audio::Sound* sound;
        const char* name;
    };
    const SoundFile sounds[] = {
        {&roar, "engine_roar"}, {&whine, "engine_whine"}, {&afterburner, "afterburner"},
        {&wind, "wind"},        {&buffet, "buffet"},      {&abLight, "ab_light"},
        {&flare, "flare"},      {&explosion, "explosion"}, {&uiMove, "ui_move"},
        {&uiSelect, "ui_select"},
        {&gunBurst, "gun_burst"}, {&gunTail, "gun_tail"}, {&missileLaunch, "missile_launch"},
        {&missileEject, "missile_eject"}, {&blastFar, "blast_far"}, {&hit, "hit"},
        {&toneSeek, "tone_seek"}, {&toneLock, "tone_lock"},
        {&rwrNew, "rwr_new"}, {&rwrLock, "rwr_lock"}, {&rwrMissile, "rwr_missile"},
    };
    for (const auto& f : sounds)
        if (!loadSound(*f.sound, f.name)) return false;
    for (int i = 0; i < audio::kMessageCount; ++i)
        if (!loadSound(voice[i], audio::messageFile(static_cast<audio::Message>(i)))) return false;
    return true;
}

bool Assets::loadSound(audio::Sound& sound, const char* name) {
    std::snprintf(soundError_, sizeof soundError_, "assets/sfx/%s.wav", name);
    if (soundCount_ < kMaxSounds) {
        core::Blob& blob = soundData_[soundCount_];
        blob = core::loadFile(soundError_);
        if (blob && audio::parseWav(blob.data, blob.size, sound)) {
            ++soundCount_;
            return true;
        }
    }
    error = soundError_;
    return false;
}

bool Assets::loadRocks() {
    for (int i = 0; i < world::kRockShapeCount; ++i) {
        std::snprintf(pathError_, sizeof pathError_, "assets/models/rock_%s.ftr", world::kRockShapes[i].name);
        if (!rocks[i].load(pathError_)) return error = pathError_, false;
        for (int m = 0; m < rocks[i].materialCount(); ++m) {
            const char* name = rocks[i].materialName(m);
            int k = 0;
            while (k < rockTextureCount_ && std::strcmp(rockTextureNames_[k], name) != 0) ++k;
            if (k == rockTextureCount_) {
                if (k == kMaxRockTextures) return error = "rock textures", false;
                std::snprintf(pathError_, sizeof pathError_, "assets/models/%s.ftx", name);
                if (!rockTextures[k].load(pathError_)) return error = pathError_, false;
                std::snprintf(rockTextureNames_[k], sizeof rockTextureNames_[k], "%s", name);
                ++rockTextureCount_;
            }
            rockMaterials[i][m] = &rockTextures[k];
        }
    }
    return true;
}

}  // namespace scenes

