// Host-side tests for the platform-neutral flight model (docs/decisions/0010).
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
    std::printf("afterburner accelerates to ~1850 km/h in level flight\n");
    auto fm = levelFlight(3000, 250);
    ControlInput in;
    in.throttleUp = true;
    run(fm, in, 20);
    float at20 = fm.state().speed * kKmh;
    run(fm, in, 160);
    float top = fm.state().speed * kKmh;
    CHECK(fm.state().afterburner, "afterburner engaged");
    CHECK(at20 > 1300, "speed after 20 s: %.0f km/h", at20);
    CHECK(top > 1750 && top < 1950, "top speed %.0f km/h", top);
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
    CHECK(fm.state().speed > kSu35S.stallSpeed, "recovered to %.0f m/s", fm.state().speed);
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
                 same(&sa.loadFactor, &sb.loadFactor, sizeof sa.loadFactor) && sa.afterburner == sb.afterburner &&
                 sa.airbrake == sb.airbrake && sa.stalled == sb.stalled && sa.crashed == sb.crashed;
    CHECK(equal, "states differ");
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

}  // namespace

int main() {
    testLevelCruise();
    testAfterburnerTopSpeed();
    testAirbrakeSlowsDown();
    testClimbTradesSpeed();
    testTurnRespectsGLimit();
    testRollRate();
    testControlDirections();
    testStallDropsNose();
    testCrashIntoSea();
    testDeterminism();
    testChaseCameraBehind();
    std::printf(g_failures ? "\n%d FAILED\n" : "\nALL PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
