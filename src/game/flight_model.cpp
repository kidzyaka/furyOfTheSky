#include "game/flight_model.h"

#include <algorithm>
#include <cmath>

namespace game {

using math::approach;
using math::clamp;
using math::kGravity;
using math::Quat;
using math::Vec3;

namespace {
constexpr Vec3 kGravityVec{0.0f, -kGravity, 0.0f};
constexpr float kMinAirSpeed = 8.0f;  // m/s: below it AoA and path rates are meaningless
}  // namespace

float liftCoefficient(const AircraftParams& p, float alpha) {
    float a = std::fabs(alpha);
    float attached = p.clMax * std::tanh(p.clSlope / p.clMax * a);
    float separated = p.clPlate * std::sin(2.0f * a);  // negative past 90 deg
    float cl = math::lerp(attached, separated, math::smoothstep(p.alphaStall, p.alphaStall + 0.35f, a));
    return alpha < 0.0f ? -cl : cl;
}

float alphaForLift(const AircraftParams& p, float cl) {
    float r = clamp(cl / p.clMax, -0.995f, 0.995f);
    return std::atanh(r) * p.clMax / p.clSlope;
}

float waveDragFactor(float mach) {
    // A narrow peak at Mach 1 that falls off faster than v^2 grows, so the
    // drag *force* has a hump: acceleration sags just below Mach 1 and comes
    // back once through - the "barrier" the player has to punch through.
    if (mach < 1.0f) return math::smoothstep(0.9f, 1.0f, mach);
    return 1.0f - 0.85f * math::smoothstep(1.0f, 1.12f, mach);
}

float FlightModel::trimAlpha(const Vec3& liftDir, float q, const Vec3& otherForces) const {
    // Lift that cancels gravity (and the thrust component) along the lift
    // axis - for gravity alone that is g * cos(bank) * cos(climb).
    float needed = -math::dot(otherForces, liftDir);
    return alphaForLift(p_, needed / std::max(q, 1e-3f));
}

void FlightModel::setStores(float massRatio, float dragRatio) {
    massInv_ = 1.0f / clamp(massRatio, 0.5f, 2.0f);
    dragScale_ = clamp(dragRatio, 0.5f, 2.0f);
}

void FlightModel::reset(const Vec3& position, const Quat& orientation, float speed) {
    s_ = FlightState{};
    s_.position = position;
    const Quat path = orientation.normalized();
    s_.pathDir = path.forward();
    s_.speed = speed;
    s_.throttle = p_.cruiseThrottle;
    s_.mach = speed / p_.speedOfSound;
    // Trimmed start: nose raised by the AoA that holds 1 g along the path,
    // otherwise the first half second is spent sinking.
    const float q = p_.liftK * massInv_ * speed * speed;
    float alpha = clamp(trimAlpha(path.up(), q, kGravityVec), p_.alphaMin, p_.alphaMax);
    s_.orientation = (path * Quat::axisAngle(math::kRight, alpha)).normalized();
    s_.alpha = alpha;
}

void FlightModel::step(const ControlInput& in, float dt) {
    if (s_.crashed) return;
    const float v = s_.speed;
    const Vec3 vdir = s_.pathDir;

    // --- Mode. Circle below PSM speed switches the FCS limiter off; the 25%
    // hysteresis keeps a manoeuvre that speeds up a little from snapping back.
    // Above PSM speed circle only lifts the G-limiter.
    s_.psm = in.psm && v < p_.psmMaxSpeed * (s_.psm ? 1.25f : 1.0f);
    const bool gOverride = in.psm && !s_.psm;
    // Past the limiter without PSM (circle just released): the FCS recovers
    // with full thrust-vectoring authority.
    const bool recovering = !s_.psm && std::fabs(s_.alpha) > p_.alphaMax + 0.15f;

    // --- Engine: hold to boost / brake, release to fall back to cruise power.
    // PSM and recovery run the engines at MIL: thrust vectoring needs thrust.
    float target = in.throttleUp ? 1.0f
                   : in.throttleDown ? p_.idleThrottle
                   : (s_.psm || recovering) ? 1.0f
                                            : p_.cruiseThrottle;
    s_.throttle += (target - s_.throttle) * approach(p_.throttleRate, dt);
    s_.afterburner = in.throttleUp && s_.throttle > 0.92f;
    s_.airbrake = in.throttleDown;
    // Engine force is fixed, so per unit mass it grows as the stores go.
    const float thrustMass = s_.afterburner ? p_.thrustAB : s_.throttle * p_.thrustMil;
    const float thrust = thrustMass * massInv_;

    // --- Air data: the relative wind seen from the body.
    const Quat toBody = s_.orientation.conjugate();
    const Vec3 vb = toBody.rotate(vdir);
    const bool flowValid = v > kMinAirSpeed;
    s_.alpha = std::atan2(-vb.y, -vb.z);
    s_.beta = std::asin(clamp(vb.x, -1.0f, 1.0f));
    s_.mach = v / p_.speedOfSound;
    const float q = p_.liftK * massInv_ * v * v;

    // --- Forces per unit mass, world frame.
    const Vec3 nose = s_.orientation.forward();
    const Vec3 up = s_.orientation.up();
    Vec3 liftDir = math::cross(s_.orientation.right(), vdir);  // normal to the path, in the symmetry plane
    float liftLen = math::length(liftDir);
    liftDir = liftLen > 1e-3f ? liftDir / liftLen : up;
    const Vec3 sideDir = math::cross(vdir, liftDir);  // "right" of the path

    const float cl = liftCoefficient(p_, s_.alpha);
    const float sa = std::sin(s_.alpha), sb = std::sin(s_.beta);
    // Broadside "plate" drag only once the flow separates; attached flow is
    // covered by the induced term.
    const float separated = math::smoothstep(0.35f, 0.8f, std::fabs(s_.alpha));
    const float cd = p_.cd0 * dragScale_ * (1.0f + (s_.airbrake ? p_.airbrakeDrag : 0.0f)) +
                     p_.cdWave * waveDragFactor(s_.mach) + p_.cdInduced * cl * cl +
                     p_.cdPlate * (sa * sa * separated + 0.5f * sb * sb);
    // Sideslip pushes the path towards the nose, like lift does.
    const Vec3 aero = liftDir * (q * cl) - sideDir * (q * p_.sideForce * s_.beta) - vdir * (q * cd);
    const Vec3 felt = aero + nose * thrust;
    const Vec3 accel = felt + kGravityVec;
    s_.loadFactor = math::dot(felt, up) / kGravity;

    // --- How fast the flight path itself turns, seen from the body. The FCS
    // leads the nose by it, so a held AoA stays held through a turn.
    const Vec3 aPerp = accel - vdir * math::dot(accel, vdir);
    const Vec3 pathRate = flowValid ? toBody.rotate(math::cross(vdir, aPerp) / v) : Vec3{};

    const float authority = clamp((v / p_.authoritySpeed) * (v / p_.authoritySpeed), 0.0f, 1.0f);
    const float thrustFrac = clamp(thrustMass / p_.thrustMil, 0.0f, 1.3f);
    Vec3 cmd;
    float response;
    if (s_.psm) {
        // Limiter off: the stick commands body rates directly and thrust
        // vectoring supplies the moment - the nose goes where the stick says
        // while the aircraft keeps sliding along its old path.
        cmd = {clamp(in.pitch, -1.0f, 1.0f) * p_.psmPitchRate, -clamp(in.yaw, -1.0f, 1.0f) * p_.psmYawRate,
               -clamp(in.roll, -1.0f, 1.0f) * p_.psmRollRate};
        response = p_.psmResponse;
    } else {
        // Normal law: the stick commands AoA between the trim (1 g, neutral
        // stick) and the limiter - the lower of the AoA limit and the AoA that
        // makes maxG at this speed. Same for sideslip with the rudder.
        const float maxG = gOverride ? p_.overrideG : p_.maxG;
        const float qSafe = std::max(q, 1e-3f);
        const float hi = std::min(p_.alphaMax, alphaForLift(p_, maxG * kGravity / qSafe));
        const float lo = std::max(p_.alphaMin, alphaForLift(p_, -p_.minG * kGravity / qSafe));
        const float trim = clamp(trimAlpha(liftDir, q, kGravityVec + nose * thrust), lo, hi);
        const float stick = clamp(in.pitch, -1.0f, 1.0f);
        const float alphaCmd = stick >= 0.0f ? math::lerp(trim, hi, stick) : math::lerp(trim, lo, -stick);
        const float betaCmd = -clamp(in.yaw, -1.0f, 1.0f) * p_.sideslipMax;
        // Control surfaces lose authority with dynamic pressure; thrust
        // vectoring does not - that is what keeps the Su-35 flyable when slow.
        // (Arcade: vectoring keeps 60% of its authority even at cruise power.)
        float pitchAuth = p_.maxPitchRate * authority + p_.tvcRate * std::max(thrustFrac, 0.6f);
        if (recovering) pitchAuth = std::max(pitchAuth, p_.recoveryRate);
        cmd.x = clamp(pathRate.x + p_.alphaGain * (alphaCmd - s_.alpha), -pitchAuth, pitchAuth);
        cmd.y = clamp(pathRate.y + p_.betaGain * (betaCmd - s_.beta), -pitchAuth, pitchAuth);
        // Roll about the velocity vector (stability axis): AoA does not turn into sideslip.
        float roll = clamp(in.roll, -1.0f, 1.0f) * p_.maxRollRate * std::max(std::sqrt(authority), 0.25f);
        cmd += (flowValid ? vb : math::kForward) * roll;
        response = p_.rateResponse;
    }
    s_.bodyRates += (cmd - s_.bodyRates) * approach(response, dt);
    s_.orientation = (s_.orientation * Quat::fromRotationVector(s_.bodyRates * dt)).normalized();  // body frame

    // Stall: too slow to hold 1 g at the AoA limit, or departed past the stall AoA.
    s_.stalled = q * liftCoefficient(p_, p_.alphaMax) < 0.9f * kGravity ||
                 std::fabs(s_.alpha) > p_.alphaStall + 0.1f;

    // --- Integrate the point mass.
    Vec3 vel = vdir * v + accel * dt;
    float speed = math::length(vel);
    if (speed > p_.maxSpeed) {
        vel *= p_.maxSpeed / speed;
        speed = p_.maxSpeed;
    }
    s_.speed = speed;
    s_.pathDir = speed > 1e-3f ? vel / speed : nose;
    s_.position += vel * dt;

    if (s_.position.y <= 0.0f) {  // sea level
        s_.position.y = 0.0f;
        s_.speed = 0.0f;
        s_.crashed = true;
    }
}

}  // namespace game
