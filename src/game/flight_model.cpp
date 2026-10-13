#include "game/flight_model.h"

#include <algorithm>

namespace game {

using math::approach;
using math::clamp;
using math::kGravity;
using math::Quat;
using math::Vec3;

void FlightModel::reset(const Vec3& position, const Quat& orientation, float speed) {
    s_ = FlightState{};
    s_.position = position;
    s_.orientation = orientation.normalized();
    s_.pathDir = s_.orientation.forward();
    s_.speed = speed;
    s_.throttle = p_.cruiseThrottle;
}

void FlightModel::step(const ControlInput& in, float dt) {
    if (s_.crashed) return;
    const float v = s_.speed;

    // --- Engine: hold to boost / brake, release to fall back to cruise power.
    float target = in.throttleUp ? 1.0f : (in.throttleDown ? p_.idleThrottle : p_.cruiseThrottle);
    s_.throttle += (target - s_.throttle) * approach(p_.throttleRate, dt);
    s_.afterburner = in.throttleUp && s_.throttle > 0.92f;
    s_.airbrake = in.throttleDown;

    // --- Control authority grows with dynamic pressure (~v^2) up to corner
    // speed; lift fades out over [0.8, 1.2] x stall speed.
    float q = v / p_.cornerSpeed;
    float authority = clamp(q * q, 0.0f, 1.0f);
    float lift = clamp((v - 0.8f * p_.stallSpeed) / (0.4f * p_.stallSpeed), 0.0f, 1.0f);
    s_.stalled = lift < 1.0f;

    // --- Commanded body rates. Above corner speed the G-limiter, not the
    // control surfaces, caps the pitch rate: omega = n * g / v.
    float gLimit = p_.maxG * kGravity / std::max(v, 1.0f);
    float pitchMax = std::min(p_.maxPitchRate * std::max(authority, 0.15f), gLimit);
    float rollMax = p_.maxRollRate * std::max(std::sqrt(authority), 0.25f);
    float yawMax = p_.maxYawRate * std::max(authority, 0.3f);
    Vec3 cmd{clamp(in.pitch, -1.0f, 1.0f) * pitchMax,   // +x: nose up
             -clamp(in.yaw, -1.0f, 1.0f) * yawMax,      // +y: nose left
             -clamp(in.roll, -1.0f, 1.0f) * rollMax};   // +z: roll left
    s_.bodyRates += (cmd - s_.bodyRates) * approach(p_.rateResponse, dt);

    // Body-frame rates: right-multiply.
    s_.orientation = (s_.orientation * Quat::fromRotationVector(s_.bodyRates * dt)).normalized();

    // --- Stall: the nose weathervanes onto the (falling) flight path.
    if (s_.stalled) {
        Quat toPath = math::rotationBetween(s_.orientation.forward(), s_.pathDir);
        float k = approach(p_.stallNoseDrop * (1.0f - lift), dt);
        s_.orientation = (math::nlerp(Quat{}, toPath, k) * s_.orientation).normalized();  // world frame
    }
    const Vec3 nose = s_.orientation.forward();

    // --- Load factor: centripetal part from the pitch rate + gravity on the lift axis.
    s_.loadFactor = v * s_.bodyRates.x / kGravity + s_.orientation.up().y;

    // --- Speed is an energy budget: thrust - drag - gravity along the path.
    float thrust = s_.afterburner ? p_.thrustAB : s_.throttle * p_.thrustMil;
    float drag = p_.dragCoef * v * v * (s_.airbrake ? p_.airbrakeFactor : 1.0f) +
                 p_.inducedDrag * s_.loadFactor * s_.loadFactor;
    float accel = thrust - drag - kGravity * s_.pathDir.y;
    s_.speed = clamp(v + accel * dt, 0.0f, p_.maxSpeed);

    // --- The path follows the nose (that is what lift does); without lift
    // gravity bends it down.
    float align = p_.alignRate * lift * std::max(authority, 0.35f);
    Vec3 vel = math::normalize(math::lerp(s_.pathDir, nose, approach(align, dt))) * s_.speed;
    vel.y -= kGravity * (1.0f - lift) * dt;
    s_.speed = math::length(vel);
    s_.pathDir = s_.speed > 1e-3f ? vel / s_.speed : nose;
    s_.position += vel * dt;

    if (s_.position.y <= 0.0f) {  // sea level
        s_.position.y = 0.0f;
        s_.speed = 0.0f;
        s_.crashed = true;
    }
}

}  // namespace game
