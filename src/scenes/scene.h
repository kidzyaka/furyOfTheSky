#pragma once

#include "audio/sound.h"
#include "audio/voice_informer.h"
#include "core/file.h"
#include "platform/psp_input.h"
#include "render/fx_draw.h"
#include "render/mesh.h"
#include "render/terrain_mesh.h"
#include "render/texture.h"
#include "world/rock_shapes.h"

namespace scenes {

// Su-35S liveries, in the order of meta::vehicle(Su35S).liveryNames.
constexpr int kLiveryCount = 2;

// Everything loaded once at boot and shared by scenes.
struct Assets {
    gfx::Mesh su35s;
    gfx::Mesh su35sLod;  // ~1500 triangles, for other aircraft (docs/decisions/0032)
    gfx::Texture liveries[kLiveryCount];
    // Missiles (tools/models/r73.py, r77.py): on the pylons and in flight.
    gfx::Mesh r73, r77;
    gfx::Texture r73Skin, r77Skin;
    // World (tools/textures/env.py): sea ripples and whitecaps, the overcast
    // deck, cumulus puff, sun disc (docs/decisions/0040, 0041).
    gfx::Texture seaDetail, seaFoam, deck, cloud, sun;
    gfx::Texture deckBase, deckStorm, deckScud;  // the overcast from below (docs/decisions/0048)
    // Rocks and islands of the map (tools/models/rocks.py, docs/decisions/0042),
    // in world::kRockShapes order: chunked LOD meshes (FTR2) and, per mesh, the
    // texture of each material - stack skins and the shared terrain details,
    // loaded once each.
    static constexpr int kMaxRockTextures = world::kRockShapeCount + 2;
    gfx::TerrainMesh rocks[world::kRockShapeCount];
    gfx::Texture rockTextures[kMaxRockTextures];
    const gfx::Texture* rockMaterials[world::kRockShapeCount][gfx::TerrainMesh::kMaxMaterials] = {};
    // Enemy patrol ship (tools/models/patrol_ship.py, docs/decisions/0045).
    gfx::Mesh ship, shipLod;
    gfx::Texture shipSkin;
    // Effects and HUD (tools/textures/fx.py).
    gfx::Texture glow, smoke, flame, ring, glint, shadow, line, ribbon;
    // Sounds (tools/audio): loops, one-shots and the voice warning messages.
    audio::Sound roar, whine, afterburner, wind, buffet;
    audio::Sound abLight, flare, explosion, uiMove, uiSelect;
    audio::Sound gunBurst, gunTail, missileLaunch, missileEject, blastFar, hit, toneSeek, toneLock;
    audio::Sound rwrNew, rwrLock, rwrMissile;  // radar warning receiver (docs/decisions/0034)
    audio::Sound voice[audio::kMessageCount];
    const char* error = nullptr;  // path of the first file that failed

    bool load();

private:
    bool loadRocks();
    char rockTextureNames_[kMaxRockTextures][16] = {};
    int rockTextureCount_ = 0;

public:
    gfx::FxTextures fx() const { return {&glow, &smoke, &flame, &ring, &ribbon, &shadow}; }

private:
    bool loadSound(audio::Sound& sound, const char* name);
    static constexpr int kMaxSounds = 48;
    core::Blob soundData_[kMaxSounds];  // the samples live here for the whole session
    int soundCount_ = 0;
    char soundError_[64] = {};
    char pathError_[64] = {};
};


// A screen of the game. Virtual dispatch is fine without RTTI.
class Scene {
public:
    virtual ~Scene() = default;
    virtual const char* name() const = 0;
    virtual void enter() {}
    // Called before switching to another scene: silence what this one started.
    virtual void leave() {}
    virtual void update(const platform::Pad& pad, float dt) = 0;
    // 3D scene and its HUD, into the frame's display list.
    virtual void render() = 0;
    // Runs on the finished frame after the GE is done: debug text, reading
    // pixels back (sun occlusion for the lens flare).
    virtual void overlay(void* drawBuffer) { (void)drawBuffer; }
};

}  // namespace scenes
