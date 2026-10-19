#include "world/environment.h"

#include <cmath>

// Presets: docs/decisions/0041 (sources for the visibility, cloud bases and
// sea states). Built as "a clear sky at this time of day", then the weather
// greys it, lowers the visibility, adds the deck, the waves and the rain.

namespace world {

namespace {

// Direction towards a body `azimuth` degrees clockwise from the start
// heading (-Z) seen from above, `elevation` degrees above the horizon.
math::Vec3 skyDirection(float azimuth, float elevation) {
    const float a = math::radians(azimuth), e = math::radians(elevation);
    return {std::sin(a) * std::cos(e), std::sin(e), -std::cos(a) * std::cos(e)};
}

Rgb grey(float v) { return {v, v, v}; }

// A clear sky at each time of day.
Environment clearSky(TimeOfDay time) {
    Environment e;
    e.time = time;
    switch (time) {
        case TimeOfDay::Day:
            // Behind-right of the start heading, ~33 deg up: the aircraft starts
            // front-lit and the sun shows in turns (docs/decisions/0011).
            e.sunDir = math::normalize({0.50f, 0.55f, 0.67f});
            e.zenith = {38, 82, 158};
            e.mid = {96, 146, 208};
            e.horizon = {186, 204, 220};
            e.glow = {255, 238, 205};
            e.glowStrength = 0.6f;
            e.sunTint = {255, 242, 222};
            e.key = {255, 242, 222};
            e.keySpecular = {150, 150, 146};
            e.fill = {58, 74, 100};
            e.bounce = {22, 42, 54};
            e.ambient = {54, 60, 70};
            e.seaDeep = {12, 40, 50};
            e.cloudLit = {255, 255, 255};
            e.cloudShade = {190, 198, 214};
            break;
        case TimeOfDay::Dawn:
            // Low ahead-left: the first sortie of the day flies into the sunrise.
            e.sunDir = skyDirection(-35.0f, 6.0f);
            e.zenith = {46, 66, 120};
            e.mid = {118, 128, 170};
            e.horizon = {226, 186, 160};
            e.glow = {255, 168, 104};
            e.glowStrength = 1.0f;
            e.sunTint = {255, 196, 140};
            e.key = {255, 178, 120};
            e.keySpecular = {170, 130, 96};
            e.fill = {54, 62, 96};
            e.bounce = {20, 32, 46};
            e.ambient = {50, 50, 64};
            e.seaDeep = {12, 30, 44};
            e.cloudLit = {255, 214, 188};
            e.cloudShade = {150, 140, 162};
            break;
        case TimeOfDay::Dusk:
            // Low ahead-right, a little lower and redder than the dawn.
            e.sunDir = skyDirection(40.0f, 4.5f);
            e.zenith = {36, 48, 98};
            e.mid = {112, 104, 146};
            e.horizon = {222, 160, 128};
            e.glow = {255, 140, 76};
            e.glowStrength = 1.1f;
            e.sunTint = {255, 170, 104};
            e.key = {255, 150, 92};
            e.keySpecular = {170, 110, 72};
            e.fill = {48, 50, 86};
            e.bounce = {18, 26, 40};
            e.ambient = {46, 42, 58};
            e.seaDeep = {10, 24, 38};
            e.cloudLit = {255, 186, 150};
            e.cloudShade = {128, 114, 140};
            break;
        case TimeOfDay::Night:
            // Moonlight: dim and blue, the disc small and white.
            e.sunDir = skyDirection(-120.0f, 32.0f);
            e.zenith = {4, 8, 20};
            e.mid = {10, 18, 36};
            e.horizon = {26, 36, 54};
            e.glow = {70, 86, 120};
            e.glowStrength = 0.25f;
            e.sunDiscSize = 0.45f;
            e.sunTint = {224, 230, 244};
            e.key = {70, 82, 110};
            e.keySpecular = {60, 66, 80};
            e.fill = {14, 20, 34};
            e.bounce = {6, 10, 16};
            e.ambient = {18, 22, 32};
            e.seaDeep = {3, 8, 14};
            e.cloudLit = {90, 98, 120};
            e.cloudShade = {30, 34, 46};
            break;
    }
    e.haze = e.horizon;
    e.hazeAbove = e.horizon;
    return e;
}

// 0..1 how bright the time of day is (greys the overcast colours with it).
float daylight(TimeOfDay t) {
    switch (t) {
        case TimeOfDay::Day: return 1.0f;
        case TimeOfDay::Dawn: return 0.72f;
        case TimeOfDay::Dusk: return 0.62f;
        default: return 0.14f;
    }
}

}  // namespace

float Environment::deckImmersion(float altitude) const {
    if (!deck) return 0.0f;
    const float edge = 60.0f;  // m of thinning at the base and the top
    return math::smoothstep(deckBase - edge, deckBase + edge, altitude) *
           (1.0f - math::smoothstep(deckTop - edge, deckTop + edge, altitude));
}

float Environment::directLight(float altitude) const {
    if (!deck) return 1.0f;
    // Under the deck the sun is a bright patch at best: diffuse light only.
    // A fog bank is thin: the sun shows through it as a pale disc.
    return math::lerp(deckFog ? 0.5f : 0.22f, 1.0f, math::smoothstep(deckBase, deckTop, altitude));
}

void Environment::hazeAt(float altitude, Rgb& colour, float& nearZ, float& farZ) const {
    const float above = deck ? math::smoothstep(deckTop - 50.0f, deckTop + 250.0f, altitude) : 0.0f;
    colour = mix(haze, hazeAbove, above);
    nearZ = math::lerp(fogNear, fogNearAbove, above);
    farZ = math::lerp(fogFar, fogFarAbove, above);
    // Inside the deck: a few hundred metres of white.
    const float in = deckImmersion(altitude);
    if (in > 0.0f) {
        colour = mix(colour, deckInside, in);
        nearZ = math::lerp(nearZ, insideNear, in);
        farZ = math::lerp(farZ, insideFar, in);
    }
}

Environment makeEnvironment(Weather weather, TimeOfDay time) {
    Environment e = clearSky(time);
    e.weather = weather;
    const float day = daylight(time);
    // The air above a deck is clear and far-seeing whatever is below it.
    e.fogNearAbove = 2500.0f;
    e.fogFarAbove = 9000.0f;
    switch (weather) {
        case Weather::Clear:
            e.fogNear = 2200.0f;
            e.fogFar = 7500.0f;
            e.cloudCount = 12;      // fair-weather cumulus, scattered
            e.cloudScale = 1.6f;
            e.waveHeight = 0.4f;   // light breeze, Beaufort 2-3: 0.2-0.6 m
            e.whitecaps = 0.01f;
            e.seaRoughness = 0.35f;
            break;
        case Weather::Cloudy:
            e.fogNear = 1500.0f;
            e.fogFar = 5500.0f;
            e.cloudCount = 20;      // cumulus mediocris, 3-4 oktas, 1-2 km across (fill rate: fewer, bigger)
            e.cloudScale = 2.5f;
            e.zenith = mix(e.zenith, e.mid, 0.25f);
            e.haze = e.horizon = mix(e.horizon, grey(180.0f * day), 0.25f);
            e.waveHeight = 1.2f;   // moderate breeze, Beaufort 4: ~1 m, W ~0.3%
            e.whitecaps = 0.04f;
            e.seaRoughness = 0.5f;
            break;
        case Weather::Overcast: {
            // Stratocumulus deck: ceiling ~1 km, the sun gone below it.
            e.deck = true;
            e.deckBase = 1100.0f;
            e.deckTop = 1700.0f;
            const Rgb below = grey(150.0f * day);
            e.haze = mix(below, Rgb{140, 152, 160}, 0.5f * day);
            // The base texture (deck_base, 0.4 cores .. 0.95 gaps) times this:
            // gaps nearly as bright as the haze, cores well darker - and the
            // fog fades it to the haze towards the horizon.
            e.deckBelow = scale(e.haze, 1.02f);
            e.deckAbove = mix(e.cloudLit, Rgb{236, 240, 246}, 0.4f);
            e.deckInside = scale(e.haze, 1.08f);
            e.fogNear = 1200.0f;
            e.fogFar = 5000.0f;
            e.cloudCount = 18;         // ragged puffs under the deck
            e.cloudBase = 700.0f;
            e.cloudTop = 950.0f;
            e.cloudLit = scale(e.haze, 0.88f);    // ragged cloud under the deck: darker than the sky
            e.cloudShade = scale(e.haze, 0.62f);
            e.seaDeep = mix(e.seaDeep, Rgb{20, 40, 44}, 0.5f);
            e.waveHeight = 2.0f;       // fresh-strong breeze, Beaufort 5-6: 2-3 m, W ~1-2%
            e.whitecaps = 0.10f;
            e.seaRoughness = 0.6f;
            e.glint = 0.0f;
            break;
        }
        case Weather::Rain: {
            // Nimbostratus to 3 km with embedded storm cells: dark, wet, rough.
            e.deck = true;
            e.deckBase = 650.0f;
            e.deckTop = 2600.0f;
            e.haze = mix(grey(112.0f * day), Rgb{96, 106, 114}, 0.5f * day);
            e.deckBelow = scale(e.haze, 1.0f);   // times deck_storm (0.42 .. 0.82)
            e.deckAbove = mix(e.cloudLit, Rgb{230, 232, 238}, 0.3f);
            e.deckInside = scale(e.haze, 1.1f);
            e.fogNear = 300.0f;
            e.fogFar = 2600.0f;
            e.cloudCount = 22;
            e.cloudBase = 420.0f;
            e.cloudTop = 560.0f;
            e.cloudLit = scale(e.haze, 0.8f);
            e.cloudShade = scale(e.haze, 0.55f);
            e.seaDeep = {10, 24, 28};
            e.waveHeight = 4.0f;       // near gale to gale, Beaufort 7-8: 4-5.5 m, W ~5-9%
            e.whitecaps = 0.35f;
            e.seaRoughness = 0.9f;
            e.glint = 0.0f;
            e.rain = 1.0f;
            e.lightning = 3.0f;
            break;
        }
        case Weather::Fog: {
            // Advection fog over cold water: a layer ~170 m deep on a calm
            // sea (sea fog is typically 100-300 m), visibility under a
            // kilometre inside it; above it the sky is clear and the islands
            // stand out of a white sea of fog. Climbing out is always possible.
            const Rgb fog = scale(mix(e.horizon, Rgb{196, 202, 206}, 0.7f), 0.4f + 0.6f * day);
            e.deck = true;
            e.deckFog = true;
            e.deckBase = -60.0f;
            e.deckTop = 170.0f;
            e.deckBelow = fog;
            e.deckInside = fog;
            e.deckAbove = scale(mix(Rgb{236, 238, 242}, e.sunTint, 0.25f), 0.55f + 0.45f * day);
            e.insideNear = 60.0f;
            e.insideFar = 950.0f;
            e.haze = fog;
            e.fogNear = 60.0f;
            e.fogFar = 950.0f;
            e.cloudCount = 6;
            e.sunDisc = 0.45f;
            e.glowStrength *= 0.5f;
            e.waveHeight = 0.2f;       // fog comes with calm air
            e.whitecaps = 0.0f;
            e.seaRoughness = 0.2f;
            e.glint = 0.3f;
            break;
        }
        case Weather::Snow: {
            e.deck = true;
            e.deckBase = 900.0f;
            e.deckTop = 2200.0f;
            e.haze = mix(grey(190.0f * day), Rgb{184, 192, 204}, 0.5f * day);
            e.deckBelow = scale(e.haze, 1.0f);   // times deck_base
            e.deckAbove = mix(e.cloudLit, Rgb{240, 242, 248}, 0.4f);
            e.deckInside = scale(e.haze, 1.08f);
            e.fogNear = 250.0f;
            e.fogFar = 2200.0f;
            e.cloudCount = 10;
            e.cloudBase = 600.0f;
            e.cloudTop = 800.0f;
            e.cloudLit = scale(e.haze, 0.88f);    // ragged cloud under the deck: darker than the sky
            e.cloudShade = scale(e.haze, 0.62f);
            e.seaDeep = {16, 32, 38};
            e.waveHeight = 2.0f;
            e.whitecaps = 0.10f;
            e.seaRoughness = 0.7f;
            e.glint = 0.0f;
            e.snow = 1.0f;
            break;
        }
    }
    e.seaHaze = e.haze;
    e.horizonGlow = e.haze;
    if (e.deck && !e.deckFog) {
        e.seaHaze = scale(e.haze, 0.74f);
        e.horizonGlow = mix(scale(e.haze, 1.22f), Rgb{236, 238, 240}, 0.12f);
    }
    if (e.deck) {
        // From below: no disc, a lighter fill from the bright ceiling, the sea
        // bounce greyer. directLight() scales the key.
        e.hazeAbove = mix(e.mid, e.horizon, 0.6f);
        e.fill = mix(e.fill, scale(e.deckBelow, 0.45f), 0.6f);
        e.ambient = mix(e.ambient, scale(e.haze, 0.42f), 0.5f);
    }
    return e;
}

}  // namespace world
