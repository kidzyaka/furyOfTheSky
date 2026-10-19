// Host-side tests for the platform-neutral flight model (docs/decisions/0014).
// Build & run: cmake -B build/host && cmake --build build/host && ctest --test-dir build/host

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <cstring>

#include "game/chase_camera.h"
#include "game/flight_model.h"

using game::ControlInput;
using game::FlightModel;
using game::kSu35S;
using math::Quat;
using math::Vec3;

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            ++g_failures;                                             \
            std::printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

constexpr float kDt = 1.0f / 60.0f;
constexpr float kKmh = 3.6f;

FlightModel levelFlight(float altitude, float speed) {
    FlightModel fm(kSu35S);
    fm.reset({0, altitude, 0}, Quat{}, speed);
    return fm;
}

void run(FlightModel& fm, const ControlInput& in, float seconds) {
    for (int i = 0, n = int(seconds / kDt); i < n; ++i) fm.step(in, kDt);
}

float pitchDeg(const FlightModel& fm) { return math::degrees(std::asin(fm.state().orientation.forward().y)); }

void testLevelCruise() {
    std::printf("level cruise holds altitude and settles at cruise speed\n");
    // Quadratic drag settles slowly (tau = 1/(2kv) ~ 40 s), so start near cruise.
    auto fm = levelFlight(1000, 250);
    run(fm, {}, 60);
    const auto& s = fm.state();
    CHECK(std::fabs(s.position.y - 1000) < 2.0f, "altitude %.1f", s.position.y);
    float kmh = s.speed * kKmh;
    CHECK(kmh > 860 && kmh < 960, "cruise %.0f km/h", kmh);
    CHECK(std::fabs(s.loadFactor - 1.0f) < 0.05f, "n = %.2f", s.loadFactor);
}

void testAfterburnerTopSpeed() {
    std::printf("afterburner punches through Mach 1 and tops out ~1750 km/h in level flight\n");
    auto fm = levelFlight(3000, 250);
    ControlInput in;
    in.throttleUp = true;
    float machOneAt = -1.0f, altAt20 = 0.0f;
    for (int i = 0; i < int(180 / kDt); ++i) {
        fm.step(in, kDt);
        if (machOneAt < 0 && fm.state().mach >= 1.0f) machOneAt = i * kDt;
        if (i == int(20 / kDt)) altAt20 = fm.state().position.y;
    }
    float top = fm.state().speed * kKmh;
    std::printf("    Mach 1 after %.1f s, top %.0f km/h, altitude after 20 s %.0f m\n", machOneAt, top, altAt20);
    CHECK(fm.state().afterburner, "afterburner engaged");
    CHECK(machOneAt > 0 && machOneAt < 25, "Mach 1 reached after %.1f s", machOneAt);
    CHECK(top > 1750 && top < 1950, "top speed %.0f km/h", top);
    // Neutral stick holds the path; while accelerating hard the trim AoA lags
    // a little behind, so a slight climb is expected (and harmless).
    CHECK(std::fabs(altAt20 - 3000) < 60, "altitude after 20 s: %.0f m", altAt20);
}

void testTransonicDragHump() {
    std::printf("the drag hump slows the acceleration around Mach 1 (the 'barrier')\n");
    auto accelAt = [](float mach) {
        auto fm = levelFlight(3000, mach * kSu35S.speedOfSound);
        ControlInput in;
        in.throttleUp = true;
        run(fm, in, 2.0f);  // spool up
        float v0 = fm.state().speed;
        run(fm, in, 0.5f);
        return (fm.state().speed - v0) / 0.5f;
    };
    float sub = accelAt(0.80f), peak = accelAt(0.99f), sup = accelAt(1.25f);
    std::printf("    accel: M0.8 %.1f, M0.99 %.1f, M1.25 %.1f m/s^2\n", sub, peak, sup);
    CHECK(peak < sub * 0.6f, "hump at M1: %.1f vs %.1f", peak, sub);
    CHECK(peak > 1.0f, "afterburner still pushes through: %.1f", peak);
    CHECK(sup > peak, "easier past the hump: %.1f", sup);
}

void testAirbrakeSlowsDown() {
    std::printf("airbrake bleeds speed quickly\n");
    auto fm = levelFlight(2000, 400);
    ControlInput in;
    in.throttleDown = true;
    run(fm, in, 5);
    float kmh = fm.state().speed * kKmh;
    CHECK(kmh < 1100, "after 5 s of braking: %.0f km/h", kmh);
}

void testClimbTradesSpeed() {
    std::printf("vertical climb without afterburner bleeds speed\n");
    FlightModel fm(kSu35S);
    fm.reset({0, 1000, 0}, Quat::axisAngle(math::kRight, math::radians(85)), 250);
    run(fm, {}, 8);
    CHECK(fm.state().speed < 200, "speed after 8 s climbing: %.0f m/s", fm.state().speed);
    CHECK(fm.state().position.y > 2000, "gained altitude: %.0f m", fm.state().position.y);
}

void testTurnRespectsGLimit() {
    std::printf("max-performance turn: G-limited at speed, ~30+ deg/s near corner speed\n");
    for (float speed : {150.0f, 200.0f, 300.0f, 450.0f}) {
        FlightModel fm(kSu35S);
        // 80 deg bank, then a full pull: mostly a horizontal turn.
        fm.reset({0, 3000, 0}, Quat::axisAngle(math::kForward, math::radians(-80)), speed);
        ControlInput in;
        in.pitch = 1.0f;
        in.throttleUp = true;
        run(fm, in, 1.0f);  // let the rates settle
        Vec3 before = fm.state().pathDir;
        float maxG = 0.0f;
        for (int i = 0; i < 60; ++i) {
            fm.step(in, kDt);
            maxG = std::max(maxG, fm.state().loadFactor);
        }
        float turnDeg = math::degrees(std::acos(math::clamp(math::dot(before, fm.state().pathDir), -1.f, 1.f)));
        std::printf("    %3.0f km/h: %.1f deg/s, peak %.1f g\n", speed * kKmh, turnDeg, maxG);
        CHECK(maxG <= kSu35S.maxG + 1.2f, "peak load %.1f g at %.0f m/s", maxG, speed);
        if (speed == 200.0f) CHECK(turnDeg > 28.0f, "turn rate at corner speed %.1f deg/s", turnDeg);
    }
}

void testRollRate() {
    std::printf("full roll input reaches ~200 deg/s at cruise\n");
    auto fm = levelFlight(2000, 250);
    ControlInput in;
    in.roll = 1.0f;
    run(fm, in, 0.6f);  // spin-up
    Vec3 up0 = fm.state().orientation.up();
    fm.step(in, kDt);
    Vec3 up1 = fm.state().orientation.up();
    float rate = math::degrees(std::acos(math::clamp(math::dot(up0, up1), -1.f, 1.f))) / kDt;
    CHECK(rate > 170 && rate < 210, "roll rate %.0f deg/s", rate);
    // Right roll input must lower the right wing.
    CHECK(fm.state().orientation.right().y < 0.0f, "right wing y = %.2f", fm.state().orientation.right().y);
}

void testControlDirections() {
    std::printf("stick directions: pitch up raises the nose, yaw right turns right\n");
    auto fm = levelFlight(2000, 250);
    ControlInput in;
    in.pitch = 1.0f;
    run(fm, in, 1.0f);
    CHECK(pitchDeg(fm) > 10.0f, "nose up %.1f deg", pitchDeg(fm));

    auto fm2 = levelFlight(2000, 250);
    ControlInput yaw;
    yaw.yaw = 1.0f;
    run(fm2, yaw, 2.0f);
    CHECK(fm2.state().orientation.forward().x > 0.1f, "heading x = %.2f", fm2.state().orientation.forward().x);
}

void testStallDropsNose() {
    std::printf("stall: slow, nose-high aircraft drops its nose and recovers speed\n");
    FlightModel fm(kSu35S);
    fm.reset({0, 3000, 0}, Quat::axisAngle(math::kRight, math::radians(20)), 35);
    ControlInput in;
    in.throttleDown = true;
    run(fm, in, 1.0f);
    CHECK(fm.state().stalled, "stalled flag set at %.0f m/s", fm.state().speed);
    run(fm, {}, 8.0f);
    CHECK(pitchDeg(fm) < 0.0f || !fm.state().stalled, "nose %.1f deg, stalled %d", pitchDeg(fm), fm.state().stalled);
    CHECK(fm.state().speed > 55.0f, "recovered to %.0f m/s", fm.state().speed);
    CHECK(fm.state().position.y < 3000, "lost altitude while stalled: %.0f m", fm.state().position.y);
}

void testCrashIntoSea() {
    std::printf("diving into the sea sets the crashed flag and freezes the state\n");
    FlightModel fm(kSu35S);
    fm.reset({0, 300, 0}, Quat::axisAngle(math::kRight, math::radians(-45)), 250);
    run(fm, {}, 5);
    CHECK(fm.state().crashed, "crashed");
    CHECK(fm.state().position.y == 0.0f, "y = %.2f", fm.state().position.y);
}

void testDeterminism() {
    std::printf("identical inputs give bit-identical states (netcode prerequisite)\n");
    auto a = levelFlight(1500, 230), b = levelFlight(1500, 230);
    for (int i = 0; i < 2000; ++i) {
        ControlInput in;
        in.pitch = std::sin(i * 0.01f);
        in.roll = std::cos(i * 0.013f);
        in.throttleUp = (i / 300) % 2 == 0;
        in.psm = (i / 450) % 2 == 1;
        a.step(in, kDt);
        b.step(in, kDt);
    }
    // Field by field: memcmp over the struct would also compare padding bytes
    // after the bools, which hold stack garbage (same lesson for netcode:
    // serialise fields, never raw structs).
    const auto &sa = a.state(), &sb = b.state();
    auto same = [](const void* x, const void* y, size_t n) { return std::memcmp(x, y, n) == 0; };
    bool equal = same(&sa.position, &sb.position, sizeof sa.position) &&
                 same(&sa.orientation, &sb.orientation, sizeof sa.orientation) &&
                 same(&sa.pathDir, &sb.pathDir, sizeof sa.pathDir) && same(&sa.speed, &sb.speed, sizeof sa.speed) &&
                 same(&sa.bodyRates, &sb.bodyRates, sizeof sa.bodyRates) &&
                 same(&sa.throttle, &sb.throttle, sizeof sa.throttle) &&
                 same(&sa.loadFactor, &sb.loadFactor, sizeof sa.loadFactor) &&
                 same(&sa.alpha, &sb.alpha, sizeof sa.alpha) && same(&sa.beta, &sb.beta, sizeof sa.beta) &&
                 same(&sa.mach, &sb.mach, sizeof sa.mach) && sa.psm == sb.psm && sa.afterburner == sb.afterburner &&
                 sa.airbrake == sb.airbrake && sa.stalled == sb.stalled && sa.crashed == sb.crashed;
    CHECK(equal, "states differ");
}

void testAlphaLimiter() {
    std::printf("normal law: full pull never exceeds the AoA limiter\n");
    for (float speed : {70.0f, 120.0f, 200.0f, 350.0f}) {
        auto fm = levelFlight(3000, speed);
        ControlInput in;
        in.pitch = 1.0f;
        float maxAlpha = 0.0f;
        for (int i = 0; i < int(4 / kDt); ++i) {
            fm.step(in, kDt);
            maxAlpha = std::max(maxAlpha, fm.state().alpha);
        }
        std::printf("    %3.0f km/h: max AoA %.1f deg\n", speed * kKmh, math::degrees(maxAlpha));
        CHECK(maxAlpha < kSu35S.alphaMax + 0.04f, "AoA %.1f deg at %.0f m/s", math::degrees(maxAlpha), speed);
    }
}

float pathPitchDeg(const FlightModel& fm) { return math::degrees(std::asin(fm.state().pathDir.y)); }

void testCobra() {
    std::printf("cobra: limiter off + full pull at 450 km/h -> nose past 90 deg, path stays level\n");
    auto fm = levelFlight(3000, 125);
    ControlInput in;
    in.psm = true;
    in.pitch = 1.0f;
    float maxAlpha = 0.0f, minSpeed = 1e9f, pathMin = 0.0f, pathMax = 0.0f;
    float released = -1.0f, recovered = -1.0f;
    for (int i = 0; i < int(6 / kDt); ++i) {
        float t = i * kDt;
        if (released < 0 && (fm.state().alpha > math::radians(100) || t > 1.6f)) {
            in = ControlInput{};  // let go of everything: the FCS takes over
            released = t;
        }
        fm.step(in, kDt);
        const auto& s = fm.state();
        maxAlpha = std::max(maxAlpha, s.alpha);
        minSpeed = std::min(minSpeed, s.speed);
        pathMin = std::min(pathMin, pathPitchDeg(fm));
        pathMax = std::max(pathMax, pathPitchDeg(fm));
        if (released >= 0 && recovered < 0 && std::fabs(s.alpha) < 0.35f) recovered = t - released;
    }
    const auto& s = fm.state();
    std::printf("    max AoA %.0f deg (released at %.2f s), speed 450 -> min %.0f km/h, path %.0f..%.0f deg, "
                "recovered in %.1f s, altitude %+.0f m\n",
                math::degrees(maxAlpha), released, minSpeed * kKmh, pathMin, pathMax, recovered,
                s.position.y - 3000);
    CHECK(maxAlpha > math::radians(85), "max AoA %.0f deg", math::degrees(maxAlpha));
    CHECK(minSpeed < 125 * 0.75f, "speed bled to %.0f km/h", minSpeed * kKmh);
    CHECK(pathMax < 35 && pathMin > -40, "path pitch %.0f..%.0f deg", pathMin, pathMax);
    CHECK(recovered > 0 && recovered < 3.0f, "nose back on the path after %.1f s", recovered);
    CHECK(std::fabs(s.position.y - 3000) < 250, "altitude change %.0f m", s.position.y - 3000);
    CHECK(std::fabs(s.pathDir.x) < 0.05f, "no heading change: path x %.2f", s.pathDir.x);
    CHECK(!s.crashed && !s.psm, "flying, normal law");
}

void testKulbit() {
    std::printf("kulbit: keep pulling with the limiter off -> full somersault almost in place\n");
    auto fm = levelFlight(3000, 110);
    ControlInput in;
    in.psm = true;
    in.pitch = 1.0f;
    float turned = 0.0f, t = 0.0f, pathMin = 0.0f, pathMax = 0.0f;
    Vec3 start = fm.state().position;
    while (turned < 2.0f * math::kPi && t < 6.0f) {
        Vec3 f0 = fm.state().orientation.forward();
        Vec3 r0 = fm.state().orientation.right();
        fm.step(in, kDt);
        Vec3 f1 = fm.state().orientation.forward();
        // Signed nose rotation about the wing axis.
        turned += std::atan2(math::dot(math::cross(f0, f1), r0), math::dot(f0, f1));
        pathMin = std::min(pathMin, pathPitchDeg(fm));
        pathMax = std::max(pathMax, pathPitchDeg(fm));
        t += kDt;
    }
    Vec3 moved = fm.state().position - start;
    std::printf("    360 deg in %.1f s, path %.0f..%.0f deg, moved %.0f m forward, %+.0f m up\n", t, pathMin, pathMax,
                -moved.z, moved.y);
    CHECK(turned >= 2.0f * math::kPi && t < 4.5f, "somersault %.0f deg in %.1f s", math::degrees(turned), t);
    CHECK(pathMax < 60 && pathMin > -75, "path did not loop: %.0f..%.0f deg", pathMin, pathMax);
    CHECK(-moved.z < 450, "travelled %.0f m forward", -moved.z);
}

void testPsmSpeedGate() {
    std::printf("above PSM speed circle only lifts the G-limiter (13 g)\n");
    FlightModel fm(kSu35S);
    fm.reset({0, 3000, 0}, Quat::axisAngle(math::kForward, math::radians(-80)), 250);
    ControlInput in;
    in.psm = true;
    in.pitch = 1.0f;
    float maxG = 0.0f, maxAlpha = 0.0f;
    for (int i = 0; i < int(2 / kDt); ++i) {
        fm.step(in, kDt);
        CHECK(!fm.state().psm, "PSM engaged at %.0f m/s", fm.state().speed);
        maxG = std::max(maxG, fm.state().loadFactor);
        maxAlpha = std::max(maxAlpha, fm.state().alpha);
    }
    std::printf("    peak %.1f g, AoA %.1f deg\n", maxG, math::degrees(maxAlpha));
    CHECK(maxG > kSu35S.maxG + 0.5f && maxG < kSu35S.overrideG + 1.0f, "peak %.1f g", maxG);
}

void testSlowFlightWithTvc() {
    std::printf("thrust vectoring keeps the nose quick at 250 km/h\n");
    auto fm = levelFlight(3000, 70);
    ControlInput in;
    in.pitch = -1.0f;  // push to the negative limit first: a long way to travel
    run(fm, in, 1.0f);
    in.pitch = 1.0f;
    float t = 0.0f;
    while (fm.state().alpha < kSu35S.alphaMax - 0.03f && t < 2.0f) {
        fm.step(in, kDt);
        t += kDt;
    }
    std::printf("    AoA %.0f -> %.0f deg in %.2f s at %.0f km/h\n", math::degrees(kSu35S.alphaMin),
                math::degrees(fm.state().alpha), t, fm.state().speed * kKmh);
    CHECK(t < 1.0f, "full AoA swing took %.2f s", t);
}

void testChaseCameraFollowsPathInCobra() {
    std::printf("chase camera stays behind the flight path at high AoA\n");
    auto fm = levelFlight(3000, 125);
    game::ChaseCamera cam;
    cam.reset(fm.state());
    ControlInput in;
    in.psm = true;
    in.pitch = 1.0f;
    for (int i = 0; i < int(1.2f / kDt) && fm.state().alpha < math::radians(90); ++i) {
        fm.step(in, kDt);
        cam.update(fm.state(), kDt);
    }
    Vec3 look = math::normalize(cam.pose().target - cam.pose().eye);
    float lookPitch = math::degrees(std::asin(look.y));
    std::printf("    AoA %.0f deg, camera looks %.0f deg up\n", math::degrees(fm.state().alpha), lookPitch);
    CHECK(lookPitch < 40.0f, "camera pitched up %.0f deg with the nose", lookPitch);
}

void testChaseCameraBehind() {
    std::printf("chase camera sits behind and above, looking ahead\n");
    auto fm = levelFlight(1000, 250);
    game::ChaseCamera cam;
    cam.reset(fm.state());
    const auto& p = cam.pose();
    CHECK(p.eye.z > fm.state().position.z + 10, "eye z %.1f", p.eye.z);
    CHECK(p.eye.y > fm.state().position.y, "eye above");
    CHECK(p.target.z < fm.state().position.z, "looks ahead");
}


// A scripted flight that touches everything the stores can reach: the
// afterburner, a hard turn, the airbrake, PSM, a dive.
void scriptedFlight(FlightModel& fm, bool callSetStores) {
    for (int i = 0; i < int(30 / kDt); ++i) {
        ControlInput in;
        const float t = i * kDt;
        in.throttleUp = t < 8.0f;
        in.pitch = (t > 4.0f && t < 12.0f) ? 1.0f : (t > 20.0f ? -0.4f : 0.0f);
        in.roll = (t > 6.0f && t < 10.0f) ? 1.0f : 0.0f;
        in.throttleDown = t > 14.0f && t < 17.0f;
        in.psm = t > 17.0f && t < 18.5f;
        if (callSetStores) fm.setStores(1.0f, 1.0f);
        fm.step(in, kDt);
    }
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

void testStoresFullLoadIsReference() {
    std::printf("stores: a full load (1, 1) is the reference - the model behaves exactly as without the call\n");
    FlightModel plain(kSu35S), full(kSu35S);
    plain.reset({0, 3000, 0}, Quat{}, 220);
    full.reset({0, 3000, 0}, Quat{}, 220);
    scriptedFlight(plain, false);
    scriptedFlight(full, true);
    const auto& a = plain.state();
    const auto& b = full.state();
    CHECK(sameBits(a.position.x, b.position.x) && sameBits(a.position.y, b.position.y) &&
              sameBits(a.position.z, b.position.z) && sameBits(a.speed, b.speed) &&
              sameBits(a.orientation.w, b.orientation.w) && sameBits(a.alpha, b.alpha) &&
              sameBits(a.loadFactor, b.loadFactor),
          "diverged: speed %.6f vs %.6f, y %.4f vs %.4f", a.speed, b.speed, a.position.y, b.position.y);
    std::printf("    after 30 s: %.0f km/h, %.0f m\n", a.speed * kKmh, a.position.y);
}

struct Run {
    float tMach1 = -1.0f;   // s from 300 m/s to Mach 1 in the afterburner
    float topKmh = 0.0f;    // level afterburner top speed
    float cruiseKmh = 0.0f; // settled level cruise
    float turnRate = 0.0f;  // deg/s, a full pull at 250 m/s, first 2 s
    float speedLoss = 0.0f; // m/s lost in a 6 s full-stick turn
};

Run fly(float massRatio, float dragRatio) {
    Run r;
    {
        auto fm = levelFlight(3000, 300);
        fm.setStores(massRatio, dragRatio);
        ControlInput in;
        in.throttleUp = true;
        for (int i = 0; i < int(90 / kDt); ++i) {
            fm.step(in, kDt);
            if (r.tMach1 < 0 && fm.state().mach >= 1.0f) r.tMach1 = i * kDt;
        }
        r.topKmh = fm.state().speed * kKmh;
    }
    {
        auto fm = levelFlight(1500, 250);
        fm.setStores(massRatio, dragRatio);
        run(fm, {}, 60);
        r.cruiseKmh = fm.state().speed * kKmh;
    }
    {
        auto fm = levelFlight(3000, 250);
        fm.setStores(massRatio, dragRatio);
        ControlInput in;
        in.pitch = 1.0f;
        in.roll = 1.0f;
        run(fm, in, 0.7f);  // roll to the bank first, then pull
        in.roll = 0.0f;
        const Vec3 d0 = fm.state().pathDir;
        const float v0 = fm.state().speed;
        run(fm, in, 2.0f);
        const Vec3 d1 = fm.state().pathDir;
        r.turnRate = math::degrees(std::acos(math::clamp(math::dot(d0, d1), -1.0f, 1.0f))) / 2.0f;
        run(fm, in, 4.0f);
        r.speedLoss = v0 - fm.state().speed;
    }
    return r;
}

void testCleanAircraftFliesBetter() {
    std::printf("stores: a clean aircraft (lighter, less drag) accelerates, cruises and turns a little better\n");
    // The numbers the game uses: 24 740 kg clean vs 26 210 kg with ten missiles
    // (docs/decisions/0038), their drag area against 62 m^2 of wing.
    const float mass = 24740.0f / 26210.0f;
    const float drag = (kSu35S.cd0 - 0.1254f / 62.0f) / kSu35S.cd0;
    const Run full = fly(1.0f, 1.0f);
    const Run clean = fly(mass, drag);
    std::printf("    Mach 1 from 300 m/s: %.1f s -> %.1f s;  top speed %.0f -> %.0f km/h;  cruise %.0f -> %.0f km/h\n",
                full.tMach1, clean.tMach1, full.topKmh, clean.topKmh, full.cruiseKmh, clean.cruiseKmh);
    std::printf("    pull at 250 m/s: %.1f -> %.1f deg/s, bleed in a 6 s turn %.0f -> %.0f m/s\n", full.turnRate,
                clean.turnRate, full.speedLoss, clean.speedLoss);
    CHECK(full.tMach1 > 0 && clean.tMach1 > 0, "Mach 1 not reached");
    CHECK(clean.tMach1 < full.tMach1 * 0.97f, "acceleration through Mach 1: %.1f s vs %.1f s", clean.tMach1, full.tMach1);
    CHECK(clean.topKmh > full.topKmh + 15.0f, "top speed %.0f vs %.0f km/h", clean.topKmh, full.topKmh);
    CHECK(clean.cruiseKmh > full.cruiseKmh + 8.0f, "cruise %.0f vs %.0f km/h", clean.cruiseKmh, full.cruiseKmh);
    CHECK(clean.speedLoss < full.speedLoss, "energy bled in the turn: %.0f vs %.0f", clean.speedLoss, full.speedLoss);
    // (The pull-rate itself is the G-limiter's: a faster aircraft turns no quicker, so it is only printed.)
    // ... but it is a modest gain, not a different aircraft.
    CHECK(clean.topKmh < full.topKmh * 1.08f && clean.tMach1 > full.tMach1 * 0.7f, "gain too big");
    // And a heavier-than-reference load is worse, the same law the other way.
    const Run heavy = fly(1.05f, 1.1f);
    CHECK(heavy.tMach1 > full.tMach1 && heavy.topKmh < full.topKmh, "heavy: %.1f s %.0f km/h", heavy.tMach1, heavy.topKmh);
}

void testStoresScalingIsClamped() {
    std::printf("stores: absurd ratios are clamped, the model stays finite\n");
    auto fm = levelFlight(3000, 250);
    fm.setStores(0.0f, -3.0f);
    run(fm, {}, 5);
    CHECK(std::isfinite(fm.state().speed) && fm.state().speed > 100 && fm.state().speed < 700, "speed %.0f",
          fm.state().speed);
    fm.setStores(100.0f, 100.0f);
    run(fm, {}, 5);
    CHECK(std::isfinite(fm.state().speed), "speed %f", fm.state().speed);
}

void testCameraShakeOff() {
    std::printf("camera: settings.cameraShake = false removes the rumble, impulses and the FOV kick\n");
    auto fm = levelFlight(3000, 250);
    game::ChaseCamera steady, rumbling, reference;
    steady.reset(fm.state());
    rumbling.reset(fm.state());
    reference.reset(fm.state());
    steady.setShake(0.0f);
    float worstOff = 0.0f, worstOn = 0.0f, fovOff = 0.0f, fovOn = 0.0f;
    for (int i = 0; i < int(2 / kDt); ++i) {
        fm.step({}, kDt);
        // The same impulses and cannon vibration into all three; only the
        // reference gets none, as it would without them.
        for (game::ChaseCamera* c : {&steady, &rumbling}) {
            if (i % 30 == 0) c->impulse(0.5f);
            c->vibrate(0.04f);
        }
        steady.update(fm.state(), kDt);
        rumbling.update(fm.state(), kDt);
        reference.update(fm.state(), kDt);
        worstOff = std::max(worstOff, math::length(steady.pose().eye - reference.pose().eye));
        worstOn = std::max(worstOn, math::length(rumbling.pose().eye - reference.pose().eye));
        fovOff = std::max(fovOff, std::fabs(steady.pose().fovDeg - reference.pose().fovDeg));
        fovOn = std::max(fovOn, std::fabs(rumbling.pose().fovDeg - reference.pose().fovDeg));
    }
    std::printf("    eye moved by shake: off %.5f m, on %.3f m; FOV kick: off %.4f deg, on %.2f deg\n", worstOff, worstOn,
                fovOff, fovOn);
    CHECK(worstOff < 1e-4f && fovOff < 1e-3f, "shake off still moved the camera: %.5f m %.4f deg", worstOff, fovOff);
    CHECK(worstOn > 0.05f && fovOn > 3.0f, "shake on should move it (sanity): %.3f m %.2f deg", worstOn, fovOn);
}

}  // namespace

int main() {
    testLevelCruise();
    testAfterburnerTopSpeed();
    testTransonicDragHump();
    testAirbrakeSlowsDown();
    testClimbTradesSpeed();
    testTurnRespectsGLimit();
    testRollRate();
    testControlDirections();
    testStallDropsNose();
    testCrashIntoSea();
    testDeterminism();
    testAlphaLimiter();
    testCobra();
    testKulbit();
    testPsmSpeedGate();
    testSlowFlightWithTvc();
    testChaseCameraFollowsPathInCobra();
    testChaseCameraBehind();
    testStoresFullLoadIsReference();
    testCleanAircraftFliesBetter();
    testStoresScalingIsClamped();
    testCameraShakeOff();
    std::printf(g_failures ? "\n%d FAILED\n" : "\nALL PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
