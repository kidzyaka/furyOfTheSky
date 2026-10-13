// Host-side tests for the effects director and flares (docs/decisions/0016,
// 0017, 0020): the physical rules behind what the player sees.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fx/aircraft_fx.h"
#include "game/countermeasures.h"
#include "game/flight_model.h"

using game::ControlInput;
using game::FlightModel;
using game::kSu35S;
using math::Quat;
using math::Vec3;

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            std::printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

constexpr float kDt = 1.0f / 60.0f;

struct Rig {
    FlightModel fm{kSu35S};
    game::Countermeasures cm;
    fx::AircraftEffects fx;
    Rig(float altitude, float speed) {
        fm.reset({0, altitude, 0}, Quat{}, speed);
        cm.reset();
        fx.reset();
    }
    void step(const ControlInput& in) {
        fm.step(in, kDt);
        cm.update(fm.state(), in.flares, kDt);
        const auto& s = fm.state();
        fx.update(s, cm, s.position + Vec3{0, 4, 20}, math::kForward, kDt);
    }
};

void testNoVaporWhenSlow() {
    std::printf("condensation follows aerodynamic load, not AoA: none at 250 km/h even at 60+ deg AoA\n");
    Rig r(1000, 70);
    ControlInput in;
    in.psm = true;
    in.pitch = 1.0f;
    float maxVapor = 0.0f, maxAlpha = 0.0f;
    for (int i = 0; i < 60; ++i) {
        r.step(in);
        maxVapor = std::max(maxVapor, r.fx.vapor());
        maxAlpha = std::max(maxAlpha, r.fm.state().alpha);
    }
    CHECK(maxAlpha > math::radians(60), "AoA reached %.0f deg", math::degrees(maxAlpha));
    CHECK(maxVapor < 0.02f, "vapor %.2f at low speed", maxVapor);
}

void testVaporInHardTurn() {
    std::printf("hard turn at 900 km/h in humid low air condenses over the wings\n");
    Rig r(1000, 250);
    ControlInput in;
    in.pitch = 1.0f;
    float maxVapor = 0.0f;
    for (int i = 0; i < 90; ++i) {
        r.step(in);
        maxVapor = std::max(maxVapor, r.fx.vapor());
    }
    CHECK(maxVapor > 0.6f, "vapor %.2f at %.1f g", maxVapor, r.fm.state().loadFactor);
}

void testTransonicWindow() {
    std::printf("transonic condensation only around M 0.9-1.0; gone when supersonic; shock moves aft\n");
    auto settle = [](float mach, float altitude, float& transonic, float& shock) {
        Rig r(altitude, mach * kSu35S.speedOfSound);
        for (int i = 0; i < 90; ++i) r.step({});  // cruise thrust: speed barely changes in 1.5 s
        transonic = r.fx.transonic();
        shock = r.fx.shockPosition();
    };
    float t80, s80, t95, s95, t99, s99, t110, s110, tHigh, sHigh;
    settle(0.80f, 1000, t80, s80);
    settle(0.95f, 1000, t95, s95);
    settle(0.99f, 1000, t99, s99);
    settle(1.10f, 1000, t110, s110);
    settle(0.97f, 9500, tHigh, sHigh);
    std::printf("    M0.80 %.2f  M0.95 %.2f (shock %.2f)  M0.99 %.2f (shock %.2f)  M1.10 %.2f  M0.97@9.5km %.2f\n", t80,
                t95, s95, t99, s99, t110, tHigh);
    CHECK(t80 < 0.02f, "no cloud at M0.8: %.2f", t80);
    CHECK(t95 > 0.6f, "cloud at M0.95: %.2f", t95);
    CHECK(s99 > s95, "shock slides aft as Mach grows: %.2f -> %.2f", s95, s99);
    CHECK(t110 < 0.05f, "gone when supersonic: %.2f", t110);
    CHECK(tHigh < t95 * 0.5f, "dry air high up condenses less: %.2f", tHigh);
}

void testMachOneEventOnce() {
    std::printf("Mach 1 crossing is reported once (for audio), nothing more\n");
    Rig r(1500, 333);
    ControlInput in;
    in.throttleUp = true;
    int events = 0;
    for (int i = 0; i < 600; ++i) {
        r.step(in);
        events += r.fx.takeMachOne() ? 1 : 0;
    }
    CHECK(r.fm.state().mach > 1.05f, "went supersonic: M%.2f", r.fm.state().mach);
    CHECK(events == 1, "%d events", events);
}

void testFlaresClearTheAirframe() {
    std::printf("flares: salvo of 6, ignite clear of the airframe, fan out to both sides\n");
    Rig r(1000, 230);
    ControlInput in;
    in.flares = true;
    for (int i = 0; i < 40; ++i) r.step(in);  // one salvo: 6 x 0.09 s
    const auto& s = r.fm.state();
    int lit = 0, left = 0, right = 0;
    float minClearance = 1e9f;
    for (int i = 0; i < game::Countermeasures::kMaxFlares; ++i) {
        const game::Flare& f = r.cm.flare(i);
        if (!f.lit()) continue;
        ++lit;
        Vec3 body = s.orientation.conjugate().rotate(f.pos - s.position);
        (body.x > 0 ? right : left) += 1;
        minClearance = std::min(minClearance, std::fabs(body.x));
    }
    std::printf("    %d lit (%d left, %d right), stock %d, nearest %.1f m off the centreline\n", lit, left, right,
                r.cm.stock(), minClearance);
    CHECK(r.cm.stock() == game::Countermeasures::kStock - game::Countermeasures::kSalvo, "stock %d", r.cm.stock());
    CHECK(left > 0 && right > 0, "both sides: %d / %d", left, right);
    CHECK(minClearance > 0.9f, "a lit flare %.1f m from the centreline (inside the tail)", minClearance);
}

}  // namespace

int main() {
    testNoVaporWhenSlow();
    testVaporInHardTurn();
    testTransonicWindow();
    testMachOneEventOnce();
    testFlaresClearTheAirframe();
    std::printf(g_failures ? "\n%d FAILED\n" : "\nALL PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
