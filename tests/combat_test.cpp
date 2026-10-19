// Host-side tests for weapons, radar and the test target (docs/decisions/
// 0026-0029): the physics the player relies on when a missile hits or misses.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/armament.h"
#include "game/autopilot.h"
#include "game/gun.h"
#include "game/missile.h"
#include "game/objective.h"
#include "game/radar.h"
#include "game/bot.h"
#include "game/rwr.h"

using game::FlightState;
using game::Missile;
using game::MissileEnd;
using game::TargetList;
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

// An aircraft state flying straight and level along `dir` (horizontal unit).
FlightState flying(const Vec3& pos, const Vec3& dir, float speed, float throttle = 0.45f, bool ab = false) {
    FlightState s;
    s.position = pos;
    s.pathDir = dir;
    s.speed = speed;
    s.orientation = math::lookRotation(dir, math::kUp);
    s.throttle = throttle;
    s.afterburner = ab;
    s.mach = speed / 340.0f;
    return s;
}

void advance(FlightState& s, float dt) { s.position += s.velocity() * dt; }

struct Shot {
    MissileEnd end = MissileEnd::None;
    float time = 0.0f;
    float miss = 0.0f;
    float maxSpeed = 0.0f;
    bool decoyed = false;  // the seeker was on a flare at some point
};

// Fly one missile against a straight-flying target; the shooter keeps a
// perfect radar track for the datalink. Optional flares from the target.
Shot fly(const game::MissileParams& type, game::Launcher launcher, FlightState shooter, FlightState target,
         bool flares = false, float seconds = 90.0f) {
    game::Countermeasures cm;
    cm.reset();
    TargetList list;
    list.add(&target, &cm);
    game::Track link;
    link.valid = true;
    link.pos = target.position;
    link.vel = target.velocity();
    Missile m = game::launchMissile(type, launcher, shooter, {3.0f, -0.6f, 2.0f}, 0, &link);
    m.aimPos = target.position;
    m.aimVel = target.velocity();
    Shot shot;
    for (float t = 0.0f; t < seconds; t += kDt) {
        advance(target, kDt);
        cm.update(target, flares, kDt);
        link.pos = target.position;
        link.vel = target.velocity();
        const game::MissileResult r = game::stepMissile(m, list, &link, kDt);
        shot.maxSpeed = std::max(shot.maxSpeed, math::length(m.vel));
        if (m.tracking == game::SeekerTrack::Flare) shot.decoyed = true;
        if (r.end != MissileEnd::None) {
            shot.end = r.end;
            shot.time = t;
            shot.miss = r.miss;
            return shot;
        }
    }
    return shot;
}

const Vec3 kNorth{0.0f, 0.0f, -1.0f};
const Vec3 kSouth{0.0f, 0.0f, 1.0f};
const Vec3 kEast{1.0f, 0.0f, 0.0f};

void testR73TailChase() {
    std::printf("R-73 up a dry-power tail at 2 km: locks and hits within a few seconds\n");
    FlightState me = flying({0, 1500, 0}, kNorth, 250);
    FlightState him = flying({0, 1500, -2000}, kNorth, 230);
    TargetList list;
    list.add(&him, nullptr);
    Vec3 dir;
    CHECK(game::infraredLock(game::kR73, me, kNorth, list, dir) == 0, "no IR lock");
    Shot s = fly(game::kR73, game::Launcher::Rail, me, him);
    CHECK(s.end == MissileEnd::Proximity, "end %d", int(s.end));
    CHECK(s.time < 7.0f, "%.1f s to impact", s.time);
    CHECK(s.maxSpeed > 650.0f && s.maxSpeed < 950.0f, "top speed %.0f m/s (Mach 2.5 class)", s.maxSpeed);
}

void testR73HeadOn() {
    std::printf("R-73 is all-aspect: locks a dry-power target nose-on at 3.5 km, not at 6 km\n");
    FlightState me = flying({0, 1500, 0}, kNorth, 250);
    FlightState near = flying({0, 1500, -3500}, kSouth, 230);
    FlightState far = flying({0, 1500, -6000}, kSouth, 230);
    TargetList a, b;
    a.add(&near, nullptr);
    b.add(&far, nullptr);
    Vec3 dir;
    CHECK(game::infraredLock(game::kR73, me, kNorth, a, dir) == 0, "no lock at 3.5 km nose-on");
    CHECK(game::infraredLock(game::kR73, me, kNorth, b, dir) < 0, "locked at 6 km nose-on (should need a burner)");
    far.afterburner = true;
    CHECK(game::infraredLock(game::kR73, me, kNorth, b, dir) == 0, "no lock on an afterburner at 6 km");
    Shot s = fly(game::kR73, game::Launcher::Rail, me, near);
    CHECK(s.end == MissileEnd::Proximity, "end %d", int(s.end));
}

void testR73OffBoresight() {
    std::printf("R-73M seeker: +-60 deg off the nose, not 70\n");
    FlightState me = flying({0, 1500, 0}, kNorth, 250);
    const float a55 = math::radians(55.0f), a70 = math::radians(70.0f);
    FlightState t55 = flying({1500 * std::sin(a55), 1500, -1500 * std::cos(a55)}, kNorth, 230);
    FlightState t70 = flying({1500 * std::sin(a70), 1500, -1500 * std::cos(a70)}, kNorth, 230);
    TargetList a, b;
    a.add(&t55, nullptr);
    b.add(&t70, nullptr);
    Vec3 dir;
    CHECK(game::infraredLock(game::kR73, me, math::normalize(t55.position - me.position), a, dir) == 0, "55 deg");
    CHECK(game::infraredLock(game::kR73, me, math::normalize(t70.position - me.position), b, dir) < 0, "70 deg");
}

void testR77Ranges() {
    std::printf("R-77 low down: head-on hit at 15 km, out of energy at 45 km\n");
    FlightState me = flying({0, 1500, 0}, kNorth, 250);
    Shot s15 = fly(game::kR77, game::Launcher::Ejector, me, flying({0, 1500, -15000}, kSouth, 230));
    CHECK(s15.end == MissileEnd::Proximity, "15 km: end %d miss %.1f", int(s15.end), s15.miss);
    CHECK(s15.maxSpeed > 950.0f, "R-77 top speed %.0f m/s (Mach 3 class)", s15.maxSpeed);
    Shot s45 = fly(game::kR77, game::Launcher::Ejector, me, flying({0, 1500, -45000}, kNorth, 230));
    CHECK(s45.end != MissileEnd::Proximity, "45 km tail chase reached it (%.1f s)", s45.time);
}

void testEnergyBleed() {
    std::printf("turning costs energy: coasting at Mach 1.6, a missile pulling 20 g slows far faster than one flying straight\n");
    auto coast = [](float g) {
        Missile m;
        m.type = &game::kR77;
        m.pos = {0, 3000, 0};
        m.vel = {0, 0, -550};
        m.age = 30.0f;  // motor long out
        m.active = true;
        m.lost = 99.0f;  // no guidance: we command the turn ourselves
        for (int i = 0; i < 180; ++i) {
            const Vec3 fwd = math::normalize(m.vel);
            const Vec3 side = math::normalize(math::cross(fwd, math::kUp));
            m.lateral = side * (g * math::kGravity);
            const float q = game::kR77.refK * math::dot(m.vel, m.vel);
            const float cn = g * math::kGravity / q;
            const float drag = q * (game::kR77.cd0 + cn * cn / game::kR77.cnAlpha);
            m.vel += (fwd * -drag + m.lateral) * kDt;
        }
        return math::length(m.vel);
    };
    const float straight = coast(0.0f), turning = coast(20.0f);
    CHECK(straight - turning > 150.0f, "after 3 s: straight %.0f m/s, 20 g %.0f m/s", straight, turning);
}

void testFlares() {
    std::printf("flares: a dry-power tail-on target popping flares decoys the R-73; on a lit burner it does not\n");
    FlightState me = flying({0, 1500, 0}, kNorth, 250);
    Shot dry = fly(game::kR73, game::Launcher::Rail, me, flying({0, 1500, -2500}, kNorth, 230), true);
    CHECK(dry.decoyed && dry.end != MissileEnd::Proximity, "dry: decoyed %d, end %d", dry.decoyed, int(dry.end));
    Shot ab = fly(game::kR73, game::Launcher::Rail, me, flying({0, 1500, -2500}, kNorth, 230, 1.0f, true), true);
    CHECK(ab.end == MissileEnd::Proximity, "burner: end %d (decoyed %d)", int(ab.end), ab.decoyed);
    // Radar missiles ignore flares entirely.
    Shot r77 = fly(game::kR77, game::Launcher::Ejector, me, flying({0, 1500, -2500}, kNorth, 230), true);
    CHECK(r77.end == MissileEnd::Proximity && !r77.decoyed, "R-77 vs flares: end %d", int(r77.end));
}

void testRadarNotch() {
    std::printf("radar: a beaming target in look-down is lost in the notch; head-on or look-up it is seen\n");
    game::Radar radar;
    FlightState me = flying({0, 3000, 0}, kNorth, 250);
    FlightState beamLow = flying({0, 1500, -10000}, kEast, 250);
    FlightState headOnLow = flying({0, 1500, -10000}, kSouth, 250);
    FlightState beamHigh = flying({0, 5000, -10000}, kEast, 250);
    bool notched = false;
    CHECK(!radar.sees(me, beamLow, false, &notched) && notched, "beaming below: seen (notched %d)", notched);
    CHECK(radar.sees(me, headOnLow, false), "head-on below: not seen");
    CHECK(radar.sees(me, beamHigh, false), "beaming above (sky behind): not seen");
}

void testRadarVolumeAndMemory() {
    std::printf("radar: +-60 deg search, a designated target held to 120 deg; a lost track coasts 3 s\n");
    game::Radar radar;
    FlightState me = flying({0, 1500, 0}, kNorth, 250);
    FlightState side = flying({8000, 1500, -1000}, kNorth, 250);  // ~83 deg off the nose
    CHECK(!radar.sees(me, side, false), "seen at 83 deg in search");
    CHECK(radar.sees(me, side, true), "not held at 83 deg when designated");

    FlightState him = flying({0, 1500, -8000}, kSouth, 250);
    TargetList list;
    list.add(&him, nullptr);
    radar.reset();
    for (int i = 0; i < 90; ++i) radar.update(me, list, kDt);
    CHECK(radar.track(0).valid && radar.designated() == 0, "no track / designation");
    // He turns to beam us, below us: notched. The track coasts, then drops.
    him = flying(him.position + Vec3{0, -1000, 0}, kEast, 250);
    float lostAfter = -1.0f;
    for (int i = 0; i < 6 * 60; ++i) {
        advance(him, kDt);
        radar.update(me, list, kDt);
        if (!radar.track(0).valid) {
            lostAfter = i * kDt;
            break;
        }
    }
    CHECK(lostAfter > 2.5f && lostAfter < 3.5f, "dropped after %.2f s", lostAfter);
}

void testGunLead() {
    std::printf("gun: rounds fired along the lead solution hit a crossing target at 600 m\n");
    game::Gun gun;
    gun.reset();
    FlightState me = flying({0, 1500, 0}, kNorth, 0.0f);  // a fixed gun platform
    FlightState him = flying({-200, 1500, -600}, kEast, 220);
    Vec3 dir;
    float tof = 0.0f;
    CHECK(gun.lead(me, him.position, him.velocity(), dir, tof), "no lead solution");
    me.orientation = math::lookRotation(dir, math::kUp);
    TargetList list;
    list.add(&him, nullptr);
    game::HitQueue hits;
    int total = 0;
    for (int i = 0; i < 120; ++i) {
        hits.clear();
        advance(him, kDt);
        // Re-aim each step (the pilot tracks the pipper).
        if (gun.lead(me, him.position, him.velocity(), dir, tof)) me.orientation = math::lookRotation(dir, math::kUp);
        gun.update(me, i < 30, list, hits, kDt);
        total += hits.count;
    }
    CHECK(total >= 5, "%d hits from a 0.5 s burst (TOF %.2f s)", total, tof);
    CHECK(gun.rounds() == 150 - 13 || gun.rounds() == 150 - 12 || gun.rounds() == 150 - 13 + 1,
          "rounds left %d (25/s for 0.5 s)", gun.rounds());
}

game::Opponent opponentOf(const FlightState* s, const game::Missile* m = nullptr, int n = 0) {
    game::Opponent o;
    o.state = s;
    o.missiles = m;
    o.missileCount = n;
    return o;
}

void testTrainingModes() {
    std::printf("training targets: orbit holds ~1.5 km radius, no mode goes below 600 m in 90 s\n");
    game::HitQueue sink;
    for (int mode = 0; mode < static_cast<int>(game::BotMode::Easy); ++mode) {
        game::Bot bot;
        FlightState player = flying({0, 1500, 0}, kNorth, 250);
        bot.reset(player, static_cast<game::BotMode>(mode));
        float minAlt = 1e9f, maxAlt = 0.0f;
        int respawns = 0;
        bot.takeRespawned();
        for (int i = 0; i < 90 * 60; ++i) {
            advance(player, kDt);
            bot.update(opponentOf(&player), sink, kDt);
            respawns += bot.takeRespawned();
            minAlt = std::min(minAlt, bot.state().position.y);
            maxAlt = std::max(maxAlt, bot.state().position.y);
        }
        // The orbit stays where it is, so the leash brings it back once the player has flown on.
        CHECK(respawns == 0 || mode == static_cast<int>(game::BotMode::Orbit), "%s: %d respawns",
              game::botModeName(bot.mode()), respawns);
        CHECK(minAlt > 600.0f, "%s: down to %.0f m", game::botModeName(bot.mode()), minAlt);
        CHECK(maxAlt < 2600.0f, "%s: up to %.0f m", game::botModeName(bot.mode()), maxAlt);
    }
    game::Bot bot;
    FlightState player = flying({0, 1500, 0}, kNorth, 0.0f);
    bot.reset(player, game::BotMode::Orbit);
    const Vec3 centre = bot.state().position + Vec3{1500, 0, 0};
    float rMin = 1e9f, rMax = 0.0f;
    for (int i = 0; i < 60 * 60; ++i) {
        bot.update(opponentOf(&player), sink, kDt);
        if (i > 20 * 60) {
            Vec3 rel = bot.state().position - centre;
            rel.y = 0;
            rMin = std::min(rMin, math::length(rel));
            rMax = std::max(rMax, math::length(rel));
        }
    }
    CHECK(rMin > 1200.0f && rMax < 1800.0f, "orbit radius %.0f..%.0f m", rMin, rMax);
}

void testBotIsMortal() {
    std::printf("the enemy is mortal: shot down it explodes, and a new one comes 4 s later\n");
    game::Bot bot;
    game::HitQueue sink;
    FlightState player = flying({0, 1500, 0}, kNorth, 250);
    bot.reset(player, game::BotMode::Weave);
    bot.takeRespawned();
    bot.hit({game::HitKind::Missile, 0, {}, 100.0f});
    CHECK(!bot.alive() && bot.state().crashed && bot.takeDestroyed() && bot.kills() == 1, "not destroyed");
    bool back = false;
    float when = 0.0f;
    for (int i = 0; i < 6 * 60 && !back; ++i) {
        advance(player, kDt);
        bot.update(opponentOf(&player), sink, kDt);
        back = bot.takeRespawned();
        when = i * kDt;
    }
    CHECK(back && bot.alive() && bot.health() == 100.0f, "respawned %d after %.1f s", back, when);
    CHECK(when > 3.5f && when < 4.5f, "after %.1f s", when);
}

void testEvadeTraining() {
    std::printf("EVADE target: breaks and pops flares when a missile comes\n");
    game::Bot bot;
    game::HitQueue sink;
    FlightState player = flying({0, 1500, 0}, kNorth, 250);
    bot.reset(player, game::BotMode::Evade);
    Missile m;
    m.type = &game::kR73;
    m.pos = player.position;
    m.vel = kNorth * 700.0f;
    m.target = 0;
    bool broke = false;
    for (int i = 0; i < 4 * 60; ++i) {
        m.pos += m.vel * kDt;
        bot.update(opponentOf(&player, &m, 1), sink, kDt);
        broke |= bot.state().loadFactor > 5.0f;
    }
    CHECK(broke, "no hard break");
    CHECK(bot.flares().stock() < game::Countermeasures::kStock, "no flares");
}

// Two bots fight to the end of `seconds`; returns kills scored by each.
struct DuelResult {
    int aKills = 0, bKills = 0;
};
DuelResult duel(game::BotMode a, game::BotMode b, float seconds, float lateral) {
    game::Bot A, B;
    FlightState origin = flying({lateral, 2000, 0}, kNorth, 250);
    A.reset(origin, a);
    B.reset(A.state(), b);
    game::HitQueue onA, onB;
    DuelResult r;
    for (int i = 0; i < static_cast<int>(seconds * 60); ++i) {
        onA.clear();
        onB.clear();
        A.update(opponentOf(&B.state(), B.arms().missiles(), game::Armament::kMaxMissiles), onB, kDt);
        B.update(opponentOf(&A.state(), A.arms().missiles(), game::Armament::kMaxMissiles), onA, kDt);
        for (int k = 0; k < onB.count; ++k) B.hit(onB.items[k]);
        for (int k = 0; k < onA.count; ++k) A.hit(onA.items[k]);
    }
    r.aKills = B.kills();
    r.bKills = A.kills();
    return r;
}

void testDifficulty() {
    std::printf("difficulty: in duels HARD beats EASY and MEDIUM beats EASY (kills over 3 x 120 s)\n");
    int hardVsEasy[2] = {0, 0}, mediumVsEasy[2] = {0, 0}, hardVsMedium[2] = {0, 0};
    const float laterals[3] = {0.0f, 1500.0f, -2500.0f};
    for (float lat : laterals) {
        DuelResult r = duel(game::BotMode::Hard, game::BotMode::Easy, 120.0f, lat);
        hardVsEasy[0] += r.aKills;
        hardVsEasy[1] += r.bKills;
        r = duel(game::BotMode::Medium, game::BotMode::Easy, 120.0f, lat);
        mediumVsEasy[0] += r.aKills;
        mediumVsEasy[1] += r.bKills;
        r = duel(game::BotMode::Hard, game::BotMode::Medium, 120.0f, lat);
        hardVsMedium[0] += r.aKills;
        hardVsMedium[1] += r.bKills;
    }
    std::printf("    hard %d : %d easy, medium %d : %d easy, hard %d : %d medium\n", hardVsEasy[0], hardVsEasy[1],
                mediumVsEasy[0], mediumVsEasy[1], hardVsMedium[0], hardVsMedium[1]);
    CHECK(hardVsEasy[0] > hardVsEasy[1] && hardVsEasy[0] >= 2, "hard %d : %d easy", hardVsEasy[0], hardVsEasy[1]);
    CHECK(mediumVsEasy[0] > mediumVsEasy[1], "medium %d : %d easy", mediumVsEasy[0], mediumVsEasy[1]);
    CHECK(hardVsMedium[0] >= hardVsMedium[1], "hard %d : %d medium", hardVsMedium[0], hardVsMedium[1]);
}

void testBotShootsBack() {
    std::printf("combat bots attack a player flying straight; HARD scores within 60 s\n");
    for (int mode = static_cast<int>(game::BotMode::Easy); mode <= static_cast<int>(game::BotMode::Hard); ++mode) {
        game::Bot bot;
        game::FlightModel me(game::kSu35S);
        me.reset({0, 2000, 0}, Quat{}, 250);
        bot.reset(me.state(), static_cast<game::BotMode>(mode));
        game::HitQueue onMe;
        int hits = 0, launches = 0, bursts = 0;
        for (int i = 0; i < 60 * 60; ++i) {
            onMe.clear();
            me.step({}, kDt);
            bot.update(opponentOf(&me.state()), onMe, kDt);
            hits += onMe.count;
            launches += bot.arms().events().launches;
            bursts += bot.arms().gun().fired() > 0;
        }
        std::printf("    %s: %d launches, %d steps firing the gun, %d hits\n", game::botModeName(bot.mode()), launches,
                    bursts, hits);
        if (mode == static_cast<int>(game::BotMode::Hard)) CHECK(hits > 0, "HARD never hit a straight-flying target");
        if (mode != static_cast<int>(game::BotMode::Easy)) CHECK(launches > 0, "%s never launched", game::botModeName(bot.mode()));
    }
}

void testRwr() {
    std::printf("RWR: search sweeps, then lock; an active R-77 on us warns, a passive R-73 doesn't (no MAWS)\n");
    game::Bot bot;
    game::FlightModel me(game::kSu35S);
    me.reset({0, 2000, 0}, Quat{}, 250);
    bot.reset(me.state(), game::BotMode::Medium);
    game::Rwr rwr;
    game::HitQueue onMe;
    bool search = false, lock = false, missile = false, newContact = false;
    float lockAt = -1.0f, missileAt = -1.0f;
    for (int i = 0; i < 40 * 60 && !missile; ++i) {
        onMe.clear();
        me.step({}, kDt);
        bot.update(opponentOf(&me.state()), onMe, kDt);
        game::Emitter e;
        e.platform = &bot.state();
        e.radar = &bot.radar();
        e.arms = &bot.arms();
        rwr.update(me.state(), &e, 1, kDt);
        newContact |= rwr.newContact();
        search |= rwr.contact(0).level == game::RwrLevel::Search;
        if (!lock && rwr.contact(0).level == game::RwrLevel::Lock) {
            lock = true;
            lockAt = i * kDt;
        }
        if (rwr.missile()) {
            missile = true;
            missileAt = i * kDt;
            // The warning points at the missile.
            Vec3 toMissile;
            float best = 1e30f;
            for (int k = 0; k < game::Armament::kMaxMissiles; ++k) {
                const Missile& m = bot.arms().missile(k);
                if (!m.live()) continue;
                const float r = math::length(m.pos - me.state().position);
                if (r < best) {
                    best = r;
                    toMissile = (m.pos - me.state().position) / r;
                }
            }
            CHECK(math::dot(toMissile, rwr.missileDir()) > 0.99f, "warning bearing off");
        }
    }
    CHECK(newContact && (search || lock), "no contact");
    CHECK(lock, "never locked");
    CHECK(missile, "no missile warning in 40 s (lock at %.1f s)", lockAt);
    std::printf("    lock at %.1f s, missile warning at %.1f s\n", lockAt, missileAt);

    // A passive IR missile right behind us: silent without a plume detector.
    FlightState own = flying({0, 1500, 0}, kNorth, 250);
    FlightState shooter = flying({0, 1500, 2000}, kNorth, 250);
    game::Armament arms;
    game::Radar radar;
    arms.reset(false);
    radar.reset();
    TargetList list;
    list.add(&own, nullptr);
    game::HitQueue hits;
    game::ControlInput in;
    // Fired once the head has locked (lock-on takes kR73.lockTime, docs/decisions/0050).
    for (int i = 0; i < 180; ++i) {
        in.missile = i > 150 && i < 155;
        advance(own, kDt);
        advance(shooter, kDt);
        radar.update(shooter, list, kDt);
        arms.update(shooter, in, radar, list, hits, kDt);
    }
    game::Emitter e;
    e.platform = &shooter;
    e.radar = &radar;
    e.arms = &arms;
    game::Rwr plain, withMaws(true);
    for (int i = 0; i < 30; ++i) {
        advance(own, kDt);
        plain.update(own, &e, 1, kDt);
        withMaws.update(own, &e, 1, kDt);
    }
    CHECK(!plain.missile(), "RWR heard a passive IR missile");
    CHECK(withMaws.missile(), "a plume detector should see the burning R-73");
}

void testLaunchZone() {
    std::printf("launch zone: R-73 in zone on a tail at 2 km, out at 14 km; R-77 in zone head-on at 12 km\n");
    auto zone = [](game::MissileSlot slot, FlightState him) {
        game::Armament arm;
        game::Radar radar;
        arm.reset();
        radar.reset();
        FlightState me = flying({0, 1500, 0}, kNorth, 250);
        TargetList list;
        list.add(&him, nullptr);
        game::HitQueue hits;
        game::ControlInput in;
        if (slot != arm.selected()) {
            in.cycleWeapon = true;
            arm.update(me, in, radar, list, hits, kDt);
            in.cycleWeapon = false;
        }
        for (int i = 0; i < 4 * 60; ++i) {
            radar.update(me, list, kDt);
            arm.update(me, in, radar, list, hits, kDt);
        }
        return arm.inZone() && arm.locked();
    };
    CHECK(zone(game::MissileSlot::R73, flying({0, 1500, -2000}, kNorth, 230)), "R-73 2 km tail");
    CHECK(!zone(game::MissileSlot::R73, flying({0, 1500, -14000}, kNorth, 230)), "R-73 14 km tail");
    CHECK(zone(game::MissileSlot::R77, flying({0, 1500, -12000}, kSouth, 230)), "R-77 12 km head-on");
}

void testFiringSequence() {
    std::printf("armament: one launch per press, alternating wings; a fired station reloads after 4 s\n");
    game::Armament arm;
    game::Radar radar;
    arm.reset();
    radar.reset();
    FlightState me = flying({0, 1500, 0}, kNorth, 250);
    FlightState him = flying({0, 1500, -2000}, kNorth, 230);
    TargetList list;
    list.add(&him, nullptr);
    game::HitQueue hits;
    game::ControlInput in;
    int sides[2] = {0, 0};
    int launches = 0;
    for (int i = 0; i < 60; ++i) {
        in.missile = (i % 10) < 5;  // press, release, press...
        radar.update(me, list, kDt);
        arm.update(me, in, radar, list, hits, kDt);
        for (int k = 0; k < arm.events().launches; ++k) {
            ++launches;
            ++sides[game::kSu35SStations[arm.events().launchStation[k]].attach.x > 0 ? 0 : 1];
        }
    }
    CHECK(launches == 4, "%d launches from 6 presses with 4 R-73 on the rails", launches);
    CHECK(sides[0] == 2 && sides[1] == 2, "right %d left %d", sides[0], sides[1]);
    in.missile = false;
    for (int i = 0; i < 5 * 60; ++i) arm.update(me, in, radar, list, hits, kDt);
    CHECK(arm.ready(game::MissileSlot::R73) == 4, "%d R-73 on the rails after 5 s", arm.ready(game::MissileSlot::R73));
}

void testGradualLock() {
    std::printf("lock-on is gradual: the ring travels nose -> target, the lock comes after lockTime, not before\n");
    auto run = [](game::MissileSlot slot, float& lockAfter, bool& monotonic, float& endError) {
        game::Armament arm;
        game::Radar radar;
        arm.reset();
        radar.reset();
        FlightState me = flying({0, 1500, 0}, kNorth, 250);
        // 2.5 km out, 20 deg right of the nose, flying away: easy for both missiles.
        const Vec3 at = Vec3{std::sin(math::radians(20.0f)), 0.0f, -std::cos(math::radians(20.0f))} * 2500.0f;
        FlightState him = flying(Vec3{0, 1500, 0} + at, kNorth, 230);
        TargetList list;
        list.add(&him, nullptr);
        game::HitQueue hits;
        game::ControlInput in;
        if (slot != arm.selected()) {
            in.cycleWeapon = true;
            arm.update(me, in, radar, list, hits, kDt);
            in.cycleWeapon = false;
        }
        float started = -1.0f, prevAngle = 0.0f;
        lockAfter = -1.0f;
        monotonic = true;
        for (int i = 0; i < 3 * 60 && lockAfter < 0.0f; ++i) {
            radar.update(me, list, kDt);
            arm.update(me, in, radar, list, hits, kDt);
            const float t = i * kDt;
            if (started < 0.0f && arm.lockProgress() > 0.0f) started = t;
            const float angle = std::acos(math::clamp(math::dot(arm.lockDir(), me.orientation.forward()), -1.0f, 1.0f));
            if (angle + 1e-4f < prevAngle) monotonic = false;
            prevAngle = angle;
            if (arm.locked()) lockAfter = t - started;
        }
        endError = math::degrees(std::acos(
            math::clamp(math::dot(arm.lockDir(), math::normalize(him.position - me.position)), -1.0f, 1.0f)));
    };
    float after, err;
    bool mono;
    run(game::MissileSlot::R73, after, mono, err);
    CHECK(after >= game::kR73.lockTime - 0.05f && after < game::kR73.lockTime + 0.3f, "R-73 locked %.2f s after cueing",
          after);
    CHECK(mono, "R-73 ring went back towards the nose while acquiring");
    CHECK(err < 1.0f, "R-73 ring %.2f deg off the target at the lock", err);
    run(game::MissileSlot::R77, after, mono, err);
    CHECK(after >= game::kR77.lockTime - 0.05f && after < game::kR77.lockTime + 0.3f, "R-77 locked %.2f s after cueing",
          after);
    CHECK(mono, "R-77 ring went back towards the nose while acquiring");
    CHECK(err < 1.0f, "R-77 ring %.2f deg off the target at the lock", err);
}

void testEngagement() {
    std::printf("engagement as in the smoke-test sortie: R-73 locks within 2 s and hits, then an R-77, then the gun\n");
    game::Armament arm;
    game::Radar radar;
    game::Bot drone;
    arm.reset();
    radar.reset();
    FlightState me = flying({0, 1500, 0}, kNorth, 250);
    drone.reset(me, game::BotMode::Straight);
    game::HitQueue hits;
    game::ControlInput in;
    int missileHits = 0, shellHits = 0;
    bool lockedBy2s = false;
    for (int step = 0; step < 900; ++step) {
        in = game::ControlInput{};
        if (step >= 120 && step < 124) in.missile = true;
        if (step >= 440 && step < 443) in.cycleWeapon = true;
        if (step >= 460 && step < 464) in.missile = true;
        if (step == 540) me = flying(drone.state().position - drone.state().pathDir * 600.0f, drone.state().pathDir,
                                     drone.state().speed + 10.0f);
        if (step >= 550 && step < 590) in.gun = true;
        advance(me, kDt);
        game::HitQueue sink;
        drone.update(opponentOf(&me, arm.missiles(), game::Armament::kMaxMissiles), sink, kDt);
        TargetList list;
        list.add(&drone.state(), &drone.flares());
        radar.update(me, list, kDt);
        hits.clear();
        arm.update(me, in, radar, list, hits, kDt);
        if (step == 119) lockedBy2s = arm.locked() && arm.inZone();
        for (int i = 0; i < hits.count; ++i) {
            drone.hit(hits.items[i]);
            (hits.items[i].kind == game::HitKind::Missile ? missileHits : shellHits)++;
        }
    }
    CHECK(lockedBy2s, "R-73 not locked and in zone 2 s after the drone appeared");
    // The R-73 kills it; the R-77 already in the air loses its target; the
    // gun meets the new one that comes 4 s later.
    CHECK(missileHits >= 1, "%d missile hits", missileHits);
    CHECK(drone.kills() >= 1, "%d kills", drone.kills());
    (void)shellHits;
}

void testDeterminism() {
    std::printf("weapons are deterministic: the same shot twice ends identically\n");
    FlightState me = flying({0, 1500, 0}, kNorth, 250);
    Shot a = fly(game::kR73, game::Launcher::Rail, me, flying({0, 1500, -2500}, kNorth, 230), true);
    Shot b = fly(game::kR73, game::Launcher::Rail, me, flying({0, 1500, -2500}, kNorth, 230), true);
    CHECK(a.end == b.end && a.time == b.time, "%.4f vs %.4f", a.time, b.time);
}


// Station masks the way the menus build them: pair i = stations 2i and 2i+1.
constexpr uint16_t pairs(unsigned pairMask) {
    uint16_t m = 0;
    for (int i = 0; i < 5; ++i)
        if (pairMask & (1u << i)) m = static_cast<uint16_t>(m | (3u << (2 * i)));
    return m;
}
constexpr unsigned kWingtip = 1, kOuter = 2, kMiddle = 4, kInner = 8, kIntake = 16;

// Run an armament for `seconds`, optionally pressing the missile button
// every `pressEvery` steps (0 = never). Returns the launches it made.
int runArmament(game::Armament& arm, float seconds, int pressEvery = 0, bool cycleFirst = false) {
    game::Radar radar;
    radar.reset();
    FlightState me = flying({0, 1500, 0}, kNorth, 250);
    FlightState him = flying({0, 1500, -2000}, kNorth, 230);
    TargetList list;
    list.add(&him, nullptr);
    game::HitQueue hits;
    game::ControlInput in;
    int launches = 0;
    for (int i = 0; i < int(seconds / kDt); ++i) {
        in = game::ControlInput{};
        in.missile = pressEvery > 0 && (i % pressEvery) < 2;
        in.cycleWeapon = cycleFirst && i < 2;
        advance(me, kDt);
        radar.update(me, list, kDt);
        arm.update(me, in, radar, list, hits, kDt);
        launches += arm.events().launches;
    }
    return launches;
}

void testLoadoutStations() {
    std::printf("loadout: pylons outside the mask start empty, are never reloaded, and have no reserve\n");
    using game::MissileSlot;
    // The default is the full load of today.
    game::Armament full;
    full.reset();
    CHECK(full.ready(MissileSlot::R73) == 4 && full.ready(MissileSlot::R77) == 6, "full load %d + %d",
          full.ready(MissileSlot::R73), full.ready(MissileSlot::R77));
    CHECK(full.reserve(MissileSlot::R73) == 20 && full.reserve(MissileSlot::R77) == 42, "reserve %d + %d",
          full.reserve(MissileSlot::R73), full.reserve(MissileSlot::R77));

    // "LIGHT": the wingtip R-73 pair and the inner R-77 pair.
    game::Armament light;
    light.reset(true, pairs(kWingtip | kInner));
    bool right[game::kStationCount];
    for (int i = 0; i < game::kStationCount; ++i) right[i] = (i < 2) || (i == 6 || i == 7);
    for (int i = 0; i < game::kStationCount; ++i)
        CHECK(light.loaded(i) == right[i] && light.carried(i) == right[i], "station %d loaded %d", i, light.loaded(i));
    CHECK(light.ready(MissileSlot::R73) == 2 && light.ready(MissileSlot::R77) == 2, "ready %d + %d",
          light.ready(MissileSlot::R73), light.ready(MissileSlot::R77));
    CHECK(light.reserve(MissileSlot::R73) == 20 && light.reserve(MissileSlot::R77) == 42, "the reloads stay (Ace Combat)");
    // "DOGFIGHT" of the menus: both R-73 pairs and the inner R-77 pair.
    game::Armament dog;
    dog.reset(true, pairs(kWingtip | kOuter | kInner));
    CHECK(dog.ready(MissileSlot::R73) == 4 && dog.ready(MissileSlot::R77) == 2, "dogfight %d + %d",
          dog.ready(MissileSlot::R73), dog.ready(MissileSlot::R77));
    // Fire everything over 30 s with the reloads running: only the loaded
    // pylons ever carry a missile again.
    const int launches = runArmament(light, 30.0f, 12);
    CHECK(launches > 4, "%d launches: the reloads should have kept it firing", launches);
    for (int i = 0; i < game::kStationCount; ++i)
        CHECK(right[i] || (!light.loaded(i) && !light.carried(i)), "empty station %d got a missile", i);
    runArmament(light, 8.0f);  // let the last reloads land
    CHECK(light.ready(MissileSlot::R73) == 2 && light.ready(MissileSlot::R77) == 2, "back to %d + %d",
          light.ready(MissileSlot::R73), light.ready(MissileSlot::R77));

    // A loadout with only R-77s: no R-73 reserve, starts on the R-77 and the
    // switch refuses to go to a type that isn't on the wings.
    game::Armament radarOnly;
    radarOnly.reset(true, pairs(kMiddle | kIntake));
    CHECK(radarOnly.ready(MissileSlot::R73) == 0 && radarOnly.reserve(MissileSlot::R73) == 0, "no R-73 anything");
    CHECK(radarOnly.selected() == MissileSlot::R77, "starts on the R-77");
    runArmament(radarOnly, 0.5f, 0, true);  // SELECT pressed
    CHECK(radarOnly.selected() == MissileSlot::R77, "SELECT must not pick a missile that isn't carried");
    // ... and one with only R-73s starts on those.
    game::Armament irOnly;
    irOnly.reset(true, pairs(kWingtip));
    CHECK(irOnly.selected() == MissileSlot::R73 && irOnly.reserve(MissileSlot::R77) == 0, "R-73 only");

    // Clean: nothing to fire, nothing ever appears; the gun is still there.
    game::Armament clean;
    clean.reset(true, 0);
    CHECK(runArmament(clean, 10.0f, 12) == 0, "a clean aircraft launched something");
    CHECK(clean.ready(MissileSlot::R73) == 0 && clean.ready(MissileSlot::R77) == 0, "clean stays clean");
    CHECK(clean.gun().rounds() == 150, "the gun is not a store: %d rounds", clean.gun().rounds());
    CHECK(!clean.inZone() && !clean.locked(), "no shot with nothing under the wings");

    // Without the range reloads (bots) the mask works the same way.
    game::Armament bot;
    bot.reset(false, pairs(kWingtip));
    CHECK(bot.ready(MissileSlot::R73) == 2 && bot.reserve(MissileSlot::R73) == 0, "bot: 2 R-73, no reserve");
}

void testStoresRatios() {
    std::printf("stores: the full load is exactly the reference; fired missiles lighten the aircraft, reloads add it back\n");
    using game::MissileSlot;
    const game::AircraftParams& p = game::kSu35S;
    game::Armament arm;
    arm.reset();
    game::StoresRatios full = arm.storesRatios(p);
    CHECK(full.mass == 1.0f && full.drag == 1.0f, "full load must be exactly 1,1: %.9f %.9f", full.mass, full.drag);

    // The real numbers: 4 x 105 kg + 6 x 175 kg = 1470 kg on 24 740 kg of aircraft.
    float hangingKg = 0.0f;
    for (const game::Station& st : game::kSu35SStations) hangingKg += st.type->mass;
    CHECK(std::fabs(hangingKg - 1470.0f) < 0.01f, "missiles weigh %.0f kg", hangingKg);

    game::Armament clean;
    clean.reset(true, 0);
    const game::StoresRatios c = clean.storesRatios(p);
    const float massRatio = p.cleanMass / (p.cleanMass + hangingKg);
    std::printf("    clean: mass x%.4f, drag x%.3f\n", c.mass, c.drag);
    CHECK(std::fabs(c.mass - massRatio) < 1e-5f, "clean mass ratio %.5f vs %.5f", c.mass, massRatio);
    CHECK(c.mass > 0.93f && c.mass < 0.96f, "clean mass x%.3f", c.mass);
    CHECK(c.drag > 0.8f && c.drag < 0.92f, "clean drag x%.3f: a modest share of the zero-lift drag", c.drag);

    // Fire two R-73 (the first press goes to the wingtips): 2 x 105 kg gone.
    game::Radar radar;
    radar.reset();
    FlightState me = flying({0, 1500, 0}, kNorth, 250);
    FlightState him = flying({0, 1500, -2000}, kNorth, 230);
    TargetList list;
    list.add(&him, nullptr);
    game::HitQueue hits;
    game::ControlInput in;
    float lightest = 1.0f;
    for (int i = 0; i < 90; ++i) {
        in.missile = i == 1 || i == 20;
        advance(me, kDt);
        radar.update(me, list, kDt);
        arm.update(me, in, radar, list, hits, kDt);
        lightest = std::min(lightest, arm.storesRatios(p).mass);
    }
    const float twoGone = (p.cleanMass + hangingKg - 210.0f) / (p.cleanMass + hangingKg);
    CHECK(std::fabs(lightest - twoGone) < 1e-5f, "after two R-73: x%.5f, expected x%.5f", lightest, twoGone);
    CHECK(arm.storesRatios(p).drag < 1.0f, "a fired missile also takes its drag away");
    // 4 s later the reserve has put the missiles back on the rails.
    in.missile = false;
    for (int i = 0; i < 5 * 60; ++i) arm.update(me, in, radar, list, hits, kDt);
    const game::StoresRatios again = arm.storesRatios(p);
    CHECK(again.mass == 1.0f && again.drag == 1.0f, "reloaded: back to exactly 1,1 (%.9f %.9f)", again.mass, again.drag);
}

void testObjective() {
    std::printf("objective: win 4 s after the last enemy, lose 3 s after our aircraft; whichever comes first stands\n");
    using game::Objective;
    Objective o;
    o.reset(3);
    for (int i = 0; i < 600; ++i) o.update(kDt);
    CHECK(o.phase() == Objective::Phase::Playing && !o.done(), "nothing happened, the sortie goes on");
    o.enemyDestroyed();
    o.enemyDestroyed();
    CHECK(o.destroyed() == 2 && !o.allDestroyed() && o.phase() == Objective::Phase::Playing, "2 of 3");
    o.enemyDestroyed();
    CHECK(o.allDestroyed() && o.phase() == Objective::Phase::Won && !o.done(), "3 of 3: won, but not over yet");
    for (int i = 0; i < int(3.9f / kDt); ++i) o.update(kDt);
    CHECK(!o.done(), "still showing MISSION COMPLETE at 3.9 s");
    for (int i = 0; i < 20; ++i) o.update(kDt);
    CHECK(o.done() && o.phase() == Objective::Phase::Won, "over after 4 s");
    o.playerLost();
    CHECK(o.phase() == Objective::Phase::Won, "a win stands even if we are lost during the closing seconds");

    o.reset(2);
    o.playerLost();
    CHECK(o.phase() == Objective::Phase::Lost && !o.done(), "lost: not over yet");
    o.enemyDestroyed();
    o.enemyDestroyed();  // a missile still in the air kills the last two after we're gone
    CHECK(o.phase() == Objective::Phase::Lost, "a failure stands even if the missile in flight finishes the job");
    for (int i = 0; i < int(2.9f / kDt); ++i) o.update(kDt);
    CHECK(!o.done(), "still showing MISSION FAILED at 2.9 s");
    for (int i = 0; i < 20; ++i) o.update(kDt);
    CHECK(o.done() && o.phase() == Objective::Phase::Lost, "over after 3 s");

    // No objective (range, dogfight, free flight): it never ends by itself.
    o.reset(0);
    o.enemyDestroyed();
    o.playerLost();
    for (int i = 0; i < 1200; ++i) o.update(kDt);
    CHECK(o.phase() == Objective::Phase::Playing && !o.done() && !o.allDestroyed(), "no mission, no ending");
}

}  // namespace

int main() {
    testR73TailChase();
    testR73HeadOn();
    testR73OffBoresight();
    testR77Ranges();
    testEnergyBleed();
    testFlares();
    testRadarNotch();
    testRadarVolumeAndMemory();
    testGunLead();
    testTrainingModes();
    testBotIsMortal();
    testEvadeTraining();
    testBotShootsBack();
    testDifficulty();
    testRwr();
    testLaunchZone();
    testFiringSequence();
    testGradualLock();
    testEngagement();
    testDeterminism();
    testLoadoutStations();
    testStoresRatios();
    testObjective();
    std::printf(g_failures ? "\n%d FAILED\n" : "\nALL PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
