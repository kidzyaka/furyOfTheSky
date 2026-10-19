#pragma once

// Weather and time of day of a sortie (docs/decisions/0041): everything the
// renderer and the effects need to know about the sky, the light, the haze,
// the clouds, the sea state and the precipitation, from two words of the
// mission table. Platform-neutral (host-tested: tests/world_test.cpp).

#include <cstdint>

#include "math/vec3.h"

namespace world {

// The values are what the mission tables and the menus use (meta::Weather is
// an alias): append only, the UI maps each to an icon (scenes/menu/mission_ui.h).
enum class Weather : uint8_t { Clear, Cloudy, Overcast, Fog, Rain, Snow };
enum class TimeOfDay : uint8_t { Dawn, Day, Dusk, Night };

struct Rgb {
    float r = 0.0f, g = 0.0f, b = 0.0f;  // 0..255
};
inline Rgb mix(const Rgb& a, const Rgb& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
inline Rgb scale(const Rgb& c, float k) { return {c.r * k, c.g * k, c.b * k}; }
inline Rgb add(const Rgb& a, const Rgb& b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline uint32_t packAbgr(const Rgb& c, float alpha = 255.0f) {
    auto ch = [](float v) { return static_cast<uint32_t>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v) + 0.5f); };
    return (ch(alpha) << 24) | (ch(c.b) << 16) | (ch(c.g) << 8) | ch(c.r);
}

struct Environment {
    Weather weather = Weather::Clear;
    TimeOfDay time = TimeOfDay::Day;

    // --- Sky. The dome is a vertical gradient horizon -> mid -> zenith with a
    // warm halo around the sun; below the horizon it is the haze colour.
    math::Vec3 sunDir{0.50f, 0.55f, 0.67f};  // unit, towards the sun (the moon at night)
    Rgb zenith, mid, horizon;
    Rgb glow;                 // halo colour around the sun
    float glowStrength = 0.4f;
    float sunDisc = 1.0f;     // 0..1 the disc sprite (0 under an overcast, the moon is smaller)
    float sunDiscSize = 1.0f; // relative size of the sprite
    Rgb sunTint{255, 242, 222};

    // --- Haze: GE fog to `haze`, also the clear colour and the dome below the horizon.
    Rgb haze;
    float fogNear = 1500.0f, fogFar = 5000.0f;  // m
    // Under a cloud deck the horizon is a line, not a blur: the sea fades
    // into a darker haze (it mirrors the dark ceiling), the base of the deck
    // into a lighter glow (daylight from under its far edge). Both equal the
    // haze without a deck.
    Rgb seaHaze, horizonGlow;
    // Above a cloud deck the air is clear: another haze and visibility (hazeAt()).
    Rgb hazeAbove;
    float fogNearAbove = 2500.0f, fogFarAbove = 9000.0f;

    // --- Light on aircraft and rocks (gfx light rig): the sun, sky fill from
    // above, sea bounce from below, ambient. In direct sun; under a cloud deck
    // the scene scales the key down (directLight()).
    Rgb key, keySpecular, fill, bounce, ambient;

    // --- Cumulus puffs (render/clouds.cpp).
    int cloudCount = 42;
    float cloudBase = 1100.0f, cloudTop = 2400.0f;  // m, puff centres
    float cloudScale = 1.0f;
    Rgb cloudLit{255, 255, 255}, cloudShade{150, 160, 176};

    // --- Overcast deck: one continuous layer, opaque, flown through.
    bool deck = false;
    float deckBase = 1500.0f, deckTop = 2100.0f;  // m
    Rgb deckBelow, deckAbove, deckInside;
    // A fog bank instead of a cloud deck: it lies on the sea (base under the
    // waterline), you see a kilometre or so inside it - the sea, the rocks -
    // and the clear sky above its top. Inside visibility (GE fog) below.
    bool deckFog = false;
    float insideNear = 20.0f, insideFar = 260.0f;  // m

    // --- Sea (render/ocean.cpp, world/sea.h).
    Rgb seaDeep;              // water body colour seen straight down
    float waveHeight = 0.6f;  // m, significant height of the swell near the camera
    float whitecaps = 0.05f;  // 0..1 share of breaking crests
    float seaRoughness = 0.5f;// 0 mirror .. 1 storm: widens the sun's reflection, caps the Fresnel
    float glint = 1.0f;       // sun glitter strength

    // --- Precipitation and storm.
    float rain = 0.0f, snow = 0.0f;  // 0..1
    float lightning = 0.0f;          // mean flashes per minute

    // Is the sun (moon) direct light where the camera is? Under the deck it is
    // diffused; inside it everything is white.
    float directLight(float altitude) const;
    // 0..1 how deep inside the deck `altitude` is (0 outside).
    float deckImmersion(float altitude) const;
    // Fog for a camera at `altitude`: under, inside or above the deck.
    void hazeAt(float altitude, Rgb& colour, float& nearZ, float& farZ) const;
    // The farthest the fog ever lets anything show (sizes the sea's rings).
    float maxVisibility() const { return deck && fogFarAbove > fogFar ? fogFarAbove : fogFar; }
};

Environment makeEnvironment(Weather weather, TimeOfDay time);

}  // namespace world
