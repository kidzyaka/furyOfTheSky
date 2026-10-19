// Host-side tests for the world layer: the sea's LOD rings against the GE's
// near plane and guard band (docs/decisions/0040, docs/psp/gotchas.md #19).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/aircraft_params.h"
#include "game/autopilot.h"
#include "game/boundary.h"
#include "game/flight_model.h"
#include "game/objective.h"
#include "game/radar.h"
#include "game/ship.h"
#include "math/quat.h"
#include "math/vec3.h"
#include "world/grid_lod.h"
#include "world/missions.h"
#include "world/rock_shapes.h"
#include "world/terrain.h"

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

using math::Vec3;

struct Rng {
    uint32_t s = 0xC0FFEEu;
    float next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) * (1.0f / 16777216.0f);
    }
    float range(float a, float b) { return a + (b - a) * next(); }
};

// --- A model of what the GE does with a triangle (gotchas #5, #19): clip
// against the near plane, project into the 4096 x 4096 virtual screen with
// the 480 x 272 viewport in the middle, and drop the whole triangle if any
// vertex of the clipped polygon lands outside it.
constexpr float kNear = 3.0f;
constexpr float kCentreX = 2048.0f, kCentreY = 2048.0f;
constexpr float kHalfW = 240.0f, kHalfH = 136.0f;

struct Camera {
    Vec3 eye, right, up, fwd;
    float focal;  // px per unit of x/z (vertical fov; square pixels)
};

Camera makeCamera(const Vec3& eye, float yaw, float pitch, float roll, float fovDeg) {
    // Forward from yaw/pitch, then roll about it.
    Vec3 fwd{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    Vec3 right0 = math::normalize(math::cross(fwd, math::kUp));
    Vec3 up0 = math::cross(right0, fwd);
    Camera c;
    c.eye = eye;
    c.fwd = fwd;
    c.right = right0 * std::cos(roll) + up0 * std::sin(roll);
    c.up = up0 * std::cos(roll) - right0 * std::sin(roll);
    c.focal = kHalfH / std::tan(math::radians(fovDeg) * 0.5f);
    return c;
}

struct P2 {
    float x, y;
};

// Returns true if the GE would drop the triangle while some of it is on screen.
bool visibleDrop(const Camera& cam, const Vec3 tri[3]) {
    // Eye space: x right, y up, z depth.
    Vec3 e[3];
    for (int i = 0; i < 3; ++i) {
        Vec3 r = tri[i] - cam.eye;
        e[i] = {math::dot(r, cam.right), math::dot(r, cam.up), math::dot(r, cam.fwd)};
    }
    // Clip against z = near (Sutherland-Hodgman).
    Vec3 poly[4];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const Vec3& a = e[i];
        const Vec3& b = e[(i + 1) % 3];
        const bool ain = a.z >= kNear, bin = b.z >= kNear;
        if (ain) poly[n++] = a;
        if (ain != bin) {
            const float t = (kNear - a.z) / (b.z - a.z);
            poly[n++] = a + (b - a) * t;
        }
    }
    if (n < 3) return false;  // wholly behind the near plane: nothing to see
    P2 s[4];
    bool dropped = false;
    for (int i = 0; i < n; ++i) {
        s[i] = {kCentreX + poly[i].x / poly[i].z * cam.focal, kCentreY - poly[i].y / poly[i].z * cam.focal};
        if (s[i].x < 0.0f || s[i].x >= 4096.0f || s[i].y < 0.0f || s[i].y >= 4096.0f) dropped = true;
    }
    if (!dropped) return false;
    // On screen at all? Clip the polygon to the viewport rectangle.
    std::vector<P2> cur(s, s + n), next;
    const float lo[2] = {kCentreX - kHalfW, kCentreY - kHalfH}, hi[2] = {kCentreX + kHalfW, kCentreY + kHalfH};
    for (int axis = 0; axis < 2 && !cur.empty(); ++axis) {
        for (int side = 0; side < 2 && !cur.empty(); ++side) {
            next.clear();
            for (size_t i = 0; i < cur.size(); ++i) {
                const P2 a = cur[i], b = cur[(i + 1) % cur.size()];
                const float va = axis ? a.y : a.x, vb = axis ? b.y : b.x;
                const float lim = side ? hi[axis] : lo[axis];
                const bool ain = side ? va <= lim : va >= lim, bin = side ? vb <= lim : vb >= lim;
                if (ain) next.push_back(a);
                if (ain != bin) {
                    const float t = (lim - va) / (vb - va);
                    next.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
                }
            }
            cur.swap(next);
        }
    }
    if (cur.size() < 3) return false;
    float area = 0.0f;
    for (size_t i = 0; i < cur.size(); ++i) {
        const P2 a = cur[i], b = cur[(i + 1) % cur.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return std::fabs(area) * 0.5f > 1.0f;  // more than a pixel of hole
}

// Triangles of a flat grid tile (y = 0), both halves.
void tileTris(float x0, float z0, float s, Vec3 out[2][3]) {
    const Vec3 a{x0, 0.0f, z0}, b{x0 + s, 0.0f, z0}, c{x0, 0.0f, z0 + s}, d{x0 + s, 0.0f, z0 + s};
    out[0][0] = a; out[0][1] = c; out[0][2] = b;
    out[1][0] = b; out[1][1] = c; out[1][2] = d;
}

// Holes on screen for one camera with the LOD rings.
int ringDrops(const Camera& cam, int levels) {
    world::GridLevel lv[world::kGridMaxLevels];
    world::gridLayout(cam.eye.x, cam.eye.z, 4.0f, levels, lv);
    int drops = 0;
    for (int l = 0; l < levels; ++l)
        for (int z = 0; z < world::kGridN; ++z)
            for (int x = 0; x < world::kGridN; ++x) {
                if (!world::gridTileDrawn(lv[l], x, z)) continue;
                Vec3 t[2][3];
                tileTris(lv[l].x0 + x * lv[l].tile, lv[l].z0 + z * lv[l].tile, lv[l].tile, t);
                drops += visibleDrop(cam, t[0]) + visibleDrop(cam, t[1]);
            }
    return drops;
}

// The sea before 2026-10-14 (docs/decisions/0019): a 20 x 20 patch of 16 m
// tiles snapped to 64 m, inside a 31 x 31 ring of 320 m tiles.
int legacyDrops(const Camera& cam) {
    const float cx = std::round(cam.eye.x / 64.0f) * 64.0f, cz = std::round(cam.eye.z / 64.0f) * 64.0f;
    int drops = 0;
    Vec3 t[2][3];
    for (int z = 0; z < 20; ++z)
        for (int x = 0; x < 20; ++x) {
            tileTris(cx - 160.0f + x * 16.0f, cz - 160.0f + z * 16.0f, 16.0f, t);
            drops += visibleDrop(cam, t[0]) + visibleDrop(cam, t[1]);
        }
    for (int z = 0; z < 31; ++z)
        for (int x = 0; x < 31; ++x) {
            if (x == 15 && z == 15) continue;
            tileTris(cx - 160.0f - 15 * 320.0f + x * 320.0f, cz - 160.0f - 15 * 320.0f + z * 320.0f, 320.0f, t);
            drops += visibleDrop(cam, t[0]) + visibleDrop(cam, t[1]);
        }
    return drops;
}

Camera randomCamera(Rng& rng) {
    // Chase camera range: eye never under 3 m (chase_camera.cpp), fov 58..74
    // plus the impulse kick, any attitude.
    const float altitudes[] = {3.2f, 4.0f, 6.0f, 10.0f, 18.0f, 35.0f, 70.0f, 150.0f, 400.0f, 1200.0f};
    const float h = altitudes[static_cast<int>(rng.next() * 10) % 10] * rng.range(0.9f, 1.1f);
    const Vec3 eye{rng.range(-30000.0f, 30000.0f), h, rng.range(-30000.0f, 30000.0f)};
    return makeCamera(eye, rng.range(0.0f, 2.0f * math::kPi), rng.range(-1.5f, 0.6f), rng.range(-math::kPi, math::kPi),
                      rng.range(56.0f, 80.0f));
}

void testLayout() {
    std::printf("grid layout\n");
    Rng rng;
    for (int i = 0; i < 4000; ++i) {
        const float cx = rng.range(-40000.0f, 40000.0f), cz = rng.range(-40000.0f, 40000.0f);
        const int levels = world::gridLevelsFor(rng.range(1500.0f, 12000.0f), 4.0f);
        world::GridLevel lv[world::kGridMaxLevels];
        world::gridLayout(cx, cz, 4.0f, levels, lv);
        // The camera stands on level 0, at least 6 tiles in from its edges
        // (8 to the centre, up to 2 off it: the origin snaps to 2 tiles).
        const float ext0 = world::kGridN * lv[0].tile;
        CHECK(cx - lv[0].x0 >= 6.0f * lv[0].tile && lv[0].x0 + ext0 - cx >= 6.0f * lv[0].tile, "cam %.1f lv0 %.1f", cx,
              lv[0].x0);
        for (int l = 1; l < levels; ++l) {
            const world::GridLevel& g = lv[l];
            CHECK((g.holeX == 4 || g.holeX == 5) && (g.holeZ == 4 || g.holeZ == 5), "level %d hole %d,%d", l, g.holeX,
                  g.holeZ);
            // The hole is exactly the finer level (no gap, no overlap).
            CHECK(std::fabs(g.x0 + g.holeX * g.tile - lv[l - 1].x0) < 1e-3f, "level %d hole x", l);
            CHECK(std::fabs(g.z0 + g.holeZ * g.tile - lv[l - 1].z0) < 1e-3f, "level %d hole z", l);
            // Nearest drawn tile at least 3 tiles away from the camera.
            const float hx0 = g.x0 + g.holeX * g.tile, hz0 = g.z0 + g.holeZ * g.tile;
            const float in = std::fmin(std::fmin(cx - hx0, hx0 + world::kGridHole * g.tile - cx),
                                       std::fmin(cz - hz0, hz0 + world::kGridHole * g.tile - cz));
            CHECK(in >= 3.0f * g.tile - 1e-3f, "level %d: camera %.1f m from the ring (tile %.0f)", l, in, g.tile);
        }
        // The outermost level reaches the requested distance.
        const world::GridLevel& last = lv[levels - 1];
        const float reach = std::fmin(std::fmin(cx - last.x0, last.x0 + world::kGridN * last.tile - cx),
                                      std::fmin(cz - last.z0, last.z0 + world::kGridN * last.tile - cz));
        CHECK(reach >= 6.0f * last.tile - 1e-3f, "reach %.0f", reach);
    }
}

void testNoHolesOnScreen() {
    std::printf("sea: no dropped triangle on screen\n");
    Rng rng;
    int legacyBad = 0, ringBad = 0;
    const int kCameras = 1500;
    for (int i = 0; i < kCameras; ++i) {
        const Camera cam = randomCamera(rng);
        const int levels = world::gridLevelsFor(6000.0f, 4.0f);
        const int drops = ringDrops(cam, levels);
        if (drops) {
            ++ringBad;
            if (ringBad <= 3)
                std::printf("  rings: %d holes at eye (%.0f, %.1f, %.0f) fwd (%.2f %.2f %.2f) up (%.2f %.2f %.2f)\n",
                            drops, cam.eye.x, cam.eye.y, cam.eye.z, cam.fwd.x, cam.fwd.y, cam.fwd.z, cam.up.x,
                            cam.up.y, cam.up.z);
        }
        legacyBad += legacyDrops(cam) > 0;
    }
    std::printf("  cameras with holes: legacy %d / %d, rings %d / %d\n", legacyBad, kCameras, ringBad, kCameras);
    // The old two-grid sea really had the holes the author saw near the water...
    CHECK(legacyBad > 0, "the legacy layout should reproduce the holes");
    // ...and the rings have none.
    CHECK(ringBad == 0, "%d cameras see dropped triangles", ringBad);

    // The worst case the old layout had: low and fast (wide fov), level.
    const Camera low = makeCamera({0.0f, 12.0f, 0.0f}, 0.3f, -0.08f, 0.0f, 74.0f);
    CHECK(legacyDrops(low) > 0, "legacy low pass should drop side tiles");
    CHECK(ringDrops(low, 9) == 0, "rings low pass");
}

// --- Rocks (docs/decisions/0042): the meshes and the collision tables come
// from the same generator; check they agree, in the game's conventions.

// FTR2 (docs/formats/ftr2.md): the surface vertices of the finest chunks,
// decoded the way the GE reads them (s16 / 32768 * the mesh's scale).
struct MeshVerts {
    std::vector<Vec3> pos;
    int chunks = 0;
};

bool loadFtr(const char* path, MeshVerts& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> d;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    if (d.size() < 48 || std::memcmp(d.data(), "FTR2", 4) != 0) return false;
    uint32_t chunks, verts;
    uint16_t mats;
    float scale[3];
    std::memcpy(&chunks, &d[4], 4);
    std::memcpy(&verts, &d[8], 4);
    std::memcpy(&mats, &d[18], 2);
    std::memcpy(scale, &d[32], 12);
    const size_t tableAt = 48 + 32u * mats, vertsAt = tableAt + 64u * chunks;
    if (d.size() < vertsAt + 24u * verts) return false;
    out.chunks = static_cast<int>(chunks);
    for (uint32_t c = 0; c < chunks; ++c) {
        const unsigned char* r = &d[tableAt + 64u * c];
        uint32_t first;
        uint16_t surface, level;
        std::memcpy(&first, r + 24, 4);
        std::memcpy(&surface, r + 36, 2);
        std::memcpy(&level, r + 40, 2);
        if (level != 0) continue;
        for (uint32_t i = 0; i < surface; ++i) {
            int16_t q[3];
            std::memcpy(q, &d[vertsAt + 16u * (first + i) + 8], 6);
            out.pos.push_back({q[0] / 32768.0f * scale[0], q[1] / 32768.0f * scale[1], q[2] / 32768.0f * scale[2]});
        }
    }
    return true;
}

// On the surface: rock and air both within a short step of it (14
// directions: the axes and the cube's diagonals).
bool onSurface(const world::RockShape& s, const Vec3& p) {
    const float rho = std::sqrt(p.x * p.x + p.z * p.z);
    if (rho < 1.0f) return true;  // the apex
    const float d = std::fmax(0.5f, 0.012f * rho);
    bool in = false, out = false;
    for (int k = 0; k < 14; ++k) {
        Vec3 dir = k < 6 ? Vec3{k == 0 ? 1.0f : k == 1 ? -1.0f : 0.0f, k == 2 ? 1.0f : k == 3 ? -1.0f : 0.0f,
                                k == 4 ? 1.0f : k == 5 ? -1.0f : 0.0f}
                         : Vec3{(k & 1) ? 0.577f : -0.577f, (k & 2) ? 0.577f : -0.577f, (k & 4) ? 0.577f : -0.577f};
        const Vec3 q = p + dir * d;
        const bool inside = world::shapeInside(s, std::atan2(q.z, q.x), std::sqrt(q.x * q.x + q.z * q.z), q.y);
        in = in || inside;
        out = out || !inside;
    }
    return in && out;
}

void testRockTables() {
    std::printf("rocks: meshes lie on their collision shapes\n");
    for (int k = 0; k < world::kRockShapeCount; ++k) {
        const world::RockShape& s = world::kRockShapes[k];
        char path[512];
        std::snprintf(path, sizeof path, "%s/assets/models/rock_%s.ftr", FURY_SOURCE_DIR, s.name);
        MeshVerts m;
        if (!loadFtr(path, m)) {
            CHECK(false, "cannot read %s", path);
            continue;
        }
        int bad = 0;
        for (const Vec3& p : m.pos) bad += onSurface(s, p) ? 0 : 1;
        CHECK(bad <= static_cast<int>(m.pos.size() / 200), "%s: %d of %zu vertices off the collision surface", s.name,
              bad, m.pos.size());
    }

    // A placed, turned stack: its mesh vertices, put into the world the way
    // the renderer does (Quat about +Y), sit on what Terrain::solid tests.
    world::Terrain t;
    t.add(0, 1200.0f, -300.0f, 0.7f);
    const world::RockShape& s0 = world::kRockShapes[0];
    char path[512];
    std::snprintf(path, sizeof path, "%s/assets/models/rock_%s.ftr", FURY_SOURCE_DIR, s0.name);
    MeshVerts m;
    loadFtr(path, m);
    const math::Quat q = t.orientation(0);
    int bad = 0;
    for (const Vec3& p : m.pos) {
        if (p.y < 1.0f || p.y > s0.wallTop[0] - 1.0f) continue;
        const Vec3 axis{0.0f, p.y, 0.0f};
        const Vec3 in = axis + (p - axis) * 0.95f, out = axis + (p - axis) * 1.05f;
        const Vec3 base{1200.0f, 0.0f, -300.0f};
        if (!t.solid(base + q.rotate(in)) || t.solid(base + q.rotate(out))) ++bad;
    }
    CHECK(bad == 0, "%d vertices of the turned rock off its collision surface", bad);

    // Segments and lines of sight.
    const Vec3 c{1200.0f, 40.0f, -300.0f};
    const float f = t.segment(c + Vec3{-500.0f, 0.0f, 0.0f}, c + Vec3{500.0f, 0.0f, 0.0f});
    CHECK(f > 0.4f && f < 0.5f, "segment through the stack: %.3f", f);
    CHECK(t.segment(c + Vec3{-500.0f, 200.0f, 0.0f}, c + Vec3{500.0f, 200.0f, 0.0f}) < 0.0f, "over the apex");
    CHECK(!t.lineOfSight(c + Vec3{-3000.0f, 0.0f, 0.0f}, c + Vec3{3000.0f, 10.0f, 0.0f}), "hidden behind it");
    CHECK(t.lineOfSight(c + Vec3{-3000.0f, 0.0f, 400.0f}, c + Vec3{3000.0f, 10.0f, 400.0f}), "beside it");
    CHECK(t.solid(c) && !t.solid(c + Vec3{0.0f, 300.0f, 0.0f}), "solid inside, air above");
}

// The grid walk in Terrain::segment against a plain dense march, on an island.
void testIslandQueries() {
    std::printf("islands: grid-walk segments match a dense march; lines of sight; clearance\n");
    int island = -1;
    for (int k = 0; k < world::kRockShapeCount; ++k)
        if (std::strcmp(world::kRockShapes[k].name, "conachair") == 0) island = k;
    CHECK(island >= 0, "no conachair shape");
    if (island < 0) return;
    world::Terrain t;
    t.add(island, 0.0f, 0.0f, 0.4f);
    const world::RockShape& s = world::kRockShapes[island];
    uint32_t rng = 12345u;
    auto rnd = [&](float lo, float hi) {
        rng = rng * 1664525u + 1013904223u;
        return lo + (hi - lo) * static_cast<float>(rng >> 8) / 16777216.0f;
    };
    int disagree = 0, hits = 0, worstMiss = 0;
    float worst = 0.0f;
    for (int n = 0; n < 300; ++n) {
        const Vec3 a{rnd(-2500.0f, 2500.0f), rnd(5.0f, 600.0f), rnd(-2500.0f, 2500.0f)};
        const Vec3 b{rnd(-2500.0f, 2500.0f), rnd(5.0f, 600.0f), rnd(-2500.0f, 2500.0f)};
        if (t.solid(a)) continue;
        const float len = math::length(b - a);
        float brute = -1.0f;
        for (float u = 0.0f; u <= len; u += 0.5f)
            if (t.solid(a + (b - a) * (u / len))) {
                brute = u / len;
                break;
            }
        const float got = t.segment(a, b);
        if ((brute >= 0.0f) != (got >= 0.0f)) {
            // A graze thinner than the march step may differ; count them.
            ++disagree;
            worstMiss = std::max(worstMiss, static_cast<int>(len));
        } else if (brute >= 0.0f) {
            ++hits;
            worst = std::fmax(worst, std::fabs(brute - got) * len);
        }
    }
    CHECK(hits > 50, "only %d test segments hit the island", hits);
    CHECK(disagree <= 3, "%d segments disagree with the dense march", disagree);
    CHECK(worst < 4.5f, "contact %.1f m off the dense march", worst);
    (void)worstMiss;
    // Over it high, through it low; a column under the summit is solid.
    CHECK(t.lineOfSight({-4000.0f, s.apex + 50.0f, 0.0f}, {4000.0f, s.apex + 50.0f, 0.0f}), "over the island");
    CHECK(!t.lineOfSight({-4000.0f, 80.0f, 0.0f}, {4000.0f, 80.0f, 0.0f}), "through it at 80 m");
    // Clearance over the island: never under the real ground.
    for (int n = 0; n < 400; ++n) {
        const float x = rnd(-1800.0f, 1800.0f), z = rnd(-1800.0f, 1800.0f);
        float top = 0.0f;
        for (float y = s.apex; y > -10.0f; y -= 2.0f)
            if (t.solid({x, y, z})) {
                top = y;
                break;
            }
        CHECK(t.clearanceHeight(x, z, 0.0f) >= top - 0.5f, "clearance %.0f under the ground %.0f at (%.0f, %.0f)",
              t.clearanceHeight(x, z, 0.0f), top, x, z);
    }
}

// The radar behind an island (docs/decisions/0042): a fighter that goes
// behind Conachair is held only on the memory (MEM) and then dropped; above
// the island it stays tracked.
void testRadarBehindIsland() {
    std::printf("radar: a target behind an island is let go within the terrain memory\n");
    world::Terrain t;
    world::Arena arena;
    world::buildMap(world::MapId::NorthSea, t, arena);
    game::FlightState own, tgt;
    own.speed = tgt.speed = 250.0f;
    own.pathDir = {0.0f, 0.0f, -1.0f};             // north, nose along it (identity orientation)
    tgt.pathDir = {0.0f, 0.0f, 1.0f};              // coming at us: no Doppler notch
    game::TargetList targets;
    targets.terrain = &t;
    targets.add(&tgt, nullptr);
    game::Radar radar;
    radar.setAutoDesignate(true);
    // Both high over the island: seen.
    own.position = {-2600.0f, 1500.0f, 1000.0f};
    tgt.position = {-2600.0f, 1500.0f, -6500.0f};
    CHECK(t.lineOfSight(own.position, tgt.position), "high: the island should not block");
    for (int k = 0; k < 120; ++k) radar.update(own, targets, 1.0f / 60.0f);
    CHECK(radar.track(0).valid && radar.track(0).sinceReturn < 0.1f, "high: not tracked");
    CHECK(radar.designated() == 0, "high: not designated");
    // Both down at 150 m with Conachair between: memory, then lost.
    own.position.y = tgt.position.y = 150.0f;
    CHECK(!t.lineOfSight(own.position, tgt.position), "low: the island must block");
    float lostAt = -1.0f;
    for (int k = 0; k < 300; ++k) {
        radar.update(own, targets, 1.0f / 60.0f);
        if (!radar.track(0).valid && lostAt < 0.0f) lostAt = k / 60.0f;
    }
    // Not the full 3 s memory: the HUD box must not sit on the rock.
    CHECK(lostAt > 0.3f && lostAt < game::kTerrainMemory + 0.1f, "low: lost at %.2f s", lostAt);
    CHECK(radar.designated() < 0, "low: designation should drop with the track");
}

// The broad-phase grid changes nothing: segments and points over the whole
// map agree with testing every rock.
bool solidBrute(const world::Terrain& t, const Vec3& p) {
    for (int i = 0; i < t.count(); ++i) {
        const world::Rock& r = t.rock(i);
        const float dx = p.x - r.x, dz = p.z - r.z;
        const float lx = dx * r.cosYaw - dz * r.sinYaw, lz = dx * r.sinYaw + dz * r.cosYaw;
        if (world::shapeInside(t.shape(i), std::atan2(lz, lx), std::sqrt(lx * lx + lz * lz), p.y)) return true;
    }
    return false;
}

void testBroadPhase() {
    std::printf("terrain: the broad-phase grid agrees with testing every rock\n");
    world::Terrain t;
    world::Arena arena;
    world::buildMap(world::MapId::NorthSea, t, arena);
    uint32_t rng = 777u;
    auto rnd = [&](float lo, float hi) {
        rng = rng * 1664525u + 1013904223u;
        return lo + (hi - lo) * static_cast<float>(rng >> 8) / 16777216.0f;
    };
    int bad = 0, hits = 0;
    for (int n = 0; n < 4000; ++n) {
        const Vec3 p{rnd(-16000.0f, 16000.0f), rnd(-10.0f, 700.0f), rnd(-16000.0f, 16000.0f)};
        if (t.solid(p) != solidBrute(t, p)) ++bad;
    }
    CHECK(bad == 0, "%d points disagree", bad);
    bad = 0;
    for (int n = 0; n < 600; ++n) {
        const Vec3 a{rnd(-15000.0f, 15000.0f), rnd(5.0f, 600.0f), rnd(-15000.0f, 15000.0f)};
        // Half long sight lines, half short steps (shells, missiles).
        const float len = n & 1 ? rnd(2000.0f, 20000.0f) : rnd(5.0f, 60.0f);
        const float h = rnd(0.0f, 6.2831853f);
        const Vec3 b = a + Vec3{std::cos(h) * len, rnd(-0.02f, 0.02f) * len, std::sin(h) * len};
        if (solidBrute(t, a)) continue;
        // Reference: march every rock at 1 m.
        float ref = -1.0f;
        for (float u = 0.0f; u <= len; u += 1.0f)
            if (solidBrute(t, a + (b - a) * (u / len))) {
                ref = u / len;
                break;
            }
        const float got = t.segment(a, b);
        if ((ref >= 0.0f) != (got >= 0.0f) || (ref >= 0.0f && std::fabs(ref - got) * len > 4.5f)) {
            if (bad++ < 3) std::printf("    a (%.0f %.0f %.0f) len %.0f: ref %.4f got %.4f\n", a.x, a.y, a.z, len, ref, got);
        }
        hits += ref >= 0.0f;
    }
    CHECK(hits > 20, "only %d of the test segments hit rock", hits);
    CHECK(bad <= 2, "%d segments disagree with testing every rock", bad);
}

// Distance from (x, z) to rock i's waterline, roughly (radial).
float shoreGap(const world::Terrain& t, int i, float x, float z) {
    const float dx = x - t.rock(i).x, dz = z - t.rock(i).z;
    return std::sqrt(dx * dx + dz * dz) - t.waterlineRadius(i, std::atan2(dz, dx));
}

void testNorthSea() {
    std::printf("rocks: the North Sea archipelago\n");
    world::Terrain t;
    world::Arena arena;
    world::buildMap(world::MapId::NorthSea, t, arena);
    CHECK(t.count() >= 60, "only %d rocks", t.count());
    int big = 0;
    for (int i = 0; i < t.count(); ++i) big += t.shape(i).maxRadius > 500.0f ? 1 : 0;
    CHECK(big >= 10, "only %d islands over 1 km", big);
    // Cover everywhere a fight goes: from any point within 11 km of the
    // centre an island or stack over 100 m stands within 3.5 km.
    for (float x = -11000.0f; x <= 11000.0f; x += 1000.0f)
        for (float z = -11000.0f; z <= 11000.0f; z += 1000.0f) {
            if (x * x + z * z > 11000.0f * 11000.0f) continue;
            float best = 1e9f;
            for (int i = 0; i < t.count(); ++i)
                if (t.shape(i).apex > 100.0f) best = std::fmin(best, shoreGap(t, i, x, z));
            CHECK(best < 3500.0f, "no cover within %.0f m of (%.0f, %.0f)", best, x, z);
        }
    // Water between any two coastlines: 150 m at least (room to fly between).
    for (int i = 0; i < t.count(); ++i)
        for (int j = 0; j < t.count(); ++j) {
            if (i == j) continue;
            const float dx = t.rock(i).x - t.rock(j).x, dz = t.rock(i).z - t.rock(j).z;
            if (std::sqrt(dx * dx + dz * dz) > t.shape(i).maxRadius + t.shape(j).maxRadius + 400.0f) continue;
            float gap = 1e9f;
            for (int k = 0; k < 192; ++k) {
                const float th = 6.2831853f * k / 192.0f;
                const float r = t.waterlineRadius(i, th);
                gap = std::fmin(gap, shoreGap(t, j, t.rock(i).x + r * std::cos(th), t.rock(i).z + r * std::sin(th)));
            }
            CHECK(gap > 150.0f, "rocks %d (%s) and %d (%s): %.0f m of water", i, t.shape(i).name, j,
                  t.shape(j).name, gap);
        }
    for (int i = 0; i < t.count(); ++i) {
        const float r = std::sqrt(t.rock(i).x * t.rock(i).x + t.rock(i).z * t.rock(i).z);
        CHECK(r + t.shape(i).maxRadius < arena.warnRadius - 5000.0f, "rock %d too near the edge", i);
    }
    CHECK(!t.solid({0.0f, 900.0f, 0.0f}) && t.clearanceHeight(0.0f, 0.0f, 300.0f) == 0.0f, "training start");
    CHECK(t.clearanceHeight(0.0f, 11500.0f, 500.0f) == 0.0f, "mission entry");
}

// --- The mission-area edge (docs/decisions/0043).
void testBoundary() {
    std::printf("arena edge: warn, then turn the nose back\n");
    const world::Arena arena;
    {
        // Straight out from the centre, hands off.
        game::FlightModel m(game::kSu35S);
        m.reset({0.0f, 1500.0f, -(arena.warnRadius - 4000.0f)}, math::Quat{}, 250.0f);
        game::Boundary b;
        float maxDist = 0.0f, warnedAt = -1.0f, turnedAt = -1.0f;
        bool backInside = false;
        for (int i = 0; i < 60 * 240 && !backInside; ++i) {
            m.step(game::ControlInput{}, 1.0f / 60.0f);
            const float yaw = b.update(m.state(), arena, 1.0f / 60.0f);
            m.turnHeading(yaw);
            const math::Vec3 p = m.state().position;
            const float d = std::sqrt(p.x * p.x + p.z * p.z);
            maxDist = std::fmax(maxDist, d);
            const float t = i / 60.0f;
            if (warnedAt < 0.0f && b.zone() == game::Boundary::Zone::Warning) warnedAt = t;
            if (turnedAt < 0.0f && b.zone() == game::Boundary::Zone::Returning) turnedAt = t;
            if (turnedAt > 0.0f && b.zone() == game::Boundary::Zone::Inside) backInside = true;
        }
        CHECK(warnedAt > 0.0f && turnedAt > warnedAt, "warned at %.1f s, turned at %.1f s", warnedAt, turnedAt);
        CHECK(turnedAt - warnedAt <= game::Boundary::kGrace + 0.1f, "grace %.1f s", turnedAt - warnedAt);
        CHECK(maxDist < arena.forceRadius + 1500.0f, "flew out to %.0f m", maxDist);
        CHECK(backInside, "never came back inside");
        CHECK(!m.state().crashed && m.state().position.y > 800.0f, "the turn must not lose the aircraft (alt %.0f)",
              m.state().position.y);
    }
    {
        // Warned, turns home on its own: the game never takes over.
        game::FlightModel m(game::kSu35S);
        m.reset({0.0f, 1500.0f, -(arena.warnRadius - 500.0f)}, math::Quat{}, 250.0f);
        game::Boundary b;
        bool returned = false;
        bool turning = false;
        for (int i = 0; i < 60 * 60; ++i) {
            // The pilot reads the warning and turns home with the game's own autopilot (5 g).
            turning |= b.zone() == game::Boundary::Zone::Warning;
            game::ControlInput in;
            if (turning) {
                game::PilotDemand d;
                const math::Vec3 p = m.state().position;
                const math::Vec3 home = math::normalize({-p.x, 0.0f, -p.z});
                d.turn = game::turnTowards(m.state(), home, 5.0f);
                in = game::autopilot(m.state(), m.params(), d);
            }
            m.step(in, 1.0f / 60.0f);
            m.turnHeading(b.update(m.state(), arena, 1.0f / 60.0f));
            returned |= b.zone() == game::Boundary::Zone::Returning;
        }
        CHECK(!returned, "a pilot who turns back must not be turned");
    }
}

// --- Ships (docs/decisions/0045): they only defend against what comes down to them.
struct ShipRun {
    int gunShots = 0, launches = 0, hitsOnUs = 0;
};

ShipRun runShip(float altitude, float offset, bool rockBetween, float seconds) {
    world::Terrain terrain;
    if (rockBetween) terrain.add(1, offset * 0.5f, 0.0f, 0.0f);  // the "tower" between them
    game::Ship ship;
    const math::Vec3 route[2] = {{0.0f, 0.0f, -5000.0f}, {0.0f, 0.0f, 5000.0f}};
    ship.reset(0.0f, 0.0f, 0.0f, route, 2, 7);
    // An aircraft flying a 3 km-radius circle around the ship's area at `altitude`,
    // starting `offset` m to the side.
    game::FlightState a;
    a.position = {offset, altitude, 0.0f};
    a.speed = 220.0f;
    a.orientation = math::lookRotation(math::kForward, math::kUp);
    a.pathDir = math::kForward;
    a.throttle = 0.8f;
    game::TargetList targets;
    targets.terrain = &terrain;
    targets.add(&a, nullptr);
    ShipRun out;
    const float dt = 1.0f / 60.0f;
    // A straight pass abeam: from 6 km ahead of the ship's beam to 6 km past it,
    // then back the other way, at `offset` m to the side.
    a.position = {offset, altitude, -6000.0f};
    a.pathDir = {0.0f, 0.0f, 1.0f};
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) {
        a.position += a.pathDir * (a.speed * dt);
        if (a.position.z > 6000.0f) a.pathDir = {0.0f, 0.0f, -1.0f};
        if (a.position.z < -6000.0f) a.pathDir = {0.0f, 0.0f, 1.0f};
        a.orientation = math::lookRotation(a.pathDir, math::kUp);
        game::HitQueue hits;
        ship.update(targets, 0, false, hits, dt);
        out.gunShots += ship.gun().fired();
        out.launches += ship.weapons().events->launches;
        out.hitsOnUs += hits.count;
    }
    return out;
}

void testShips() {
    std::printf("ships: engage only low flyers, in sight\n");
    const ShipRun high = runShip(3000.0f, 2000.0f, false, 40.0f);
    CHECK(high.gunShots == 0 && high.launches == 0, "fired at a fighter at 3 km altitude (%d rounds, %d missiles)",
          high.gunShots, high.launches);
    const ShipRun low = runShip(300.0f, 2000.0f, false, 40.0f);
    CHECK(low.gunShots > 30, "didn't use the gun on a low flyer at 2 km (%d)", low.gunShots);
    CHECK(low.launches >= 1, "no missile at a low flyer at 2 km");
    CHECK(low.hitsOnUs >= 1, "a straight pass at 300 m, 2 km abeam, was never hit");
    std::printf("    low flyer at 2 km, 40 s: %d tracers, %d missiles, %d hits on it\n", low.gunShots, low.launches,
                low.hitsOnUs);
    const ShipRun far = runShip(300.0f, 9000.0f, false, 30.0f);
    CHECK(far.gunShots == 0 && far.launches == 0, "fired beyond its reach");
    const ShipRun hidden = runShip(60.0f, 2000.0f, true, 1.0f);
    CHECK(hidden.gunShots == 0 && hidden.launches == 0, "fired through a rock");

    // Three missile hits put it down; it sinks and is gone.
    game::Ship ship;
    const math::Vec3 route[1] = {{0.0f, 0.0f, -5000.0f}};
    ship.reset(0.0f, 0.0f, 0.0f, route, 1, 3);
    for (int k = 0; k < 2; ++k) ship.hit({game::HitKind::Missile, 0, ship.state().position, 100.0f});
    CHECK(ship.alive(), "two missiles sank it");
    ship.hit({game::HitKind::Missile, 0, ship.state().position, 100.0f});
    CHECK(!ship.alive() && ship.takeDestroyed() && ship.state().crashed, "three missiles didn't");
    game::TargetList none;
    game::HitQueue hits;
    for (int i = 0; i < static_cast<int>((game::Ship::kSinkTime + 1.0f) * 60.0f); ++i) ship.update(none, -1, false, hits, 1.0f / 60.0f);
    CHECK(!ship.afloat(), "still afloat after %.0f s", game::Ship::kSinkTime);
}

void testMissionLayouts() {
    std::printf("missions: entries in open air, ship routes in open water\n");
    world::Terrain t;
    world::Arena arena;
    world::buildMap(world::MapId::NorthSea, t, arena);
    for (int m = 0; m < 4; ++m) {
        const world::MissionLayout& l = world::missionLayout(0, m);
        const math::Vec3 p = l.playerStart;
        CHECK(std::sqrt(p.x * p.x + p.z * p.z) < arena.warnRadius - 3000.0f, "NS-0%d starts near the edge", m + 1);
        CHECK(p.y > t.clearanceHeight(p.x, p.z, 400.0f) + 150.0f, "NS-0%d starts too close to a rock", m + 1);
        for (int r = 0; r < l.ships; ++r) {
            const world::ShipRoute& route = l.routes[r];
            CHECK(route.count >= 2, "NS-0%d ship %d has no route", m + 1, r);
            for (int k = 0; k < route.count; ++k) {
                const math::Vec3 a = route.points[k], b = route.points[(k + 1) % route.count];
                // Every 50 m of each leg well off every coastline.
                const float len = math::length(b - a);
                float clear = 1e9f;
                int near = -1;
                for (float u = 0.0f; u <= len; u += 50.0f) {
                    const Vec3 q = a + (b - a) * (u / std::fmax(len, 1.0f));
                    for (int i = 0; i < t.count(); ++i) {
                        const float g = shoreGap(t, i, q.x, q.z);
                        if (g < clear) clear = g, near = i;
                    }
                }
                CHECK(clear > 300.0f, "NS-0%d ship %d leg %d passes %.0f m from rock %d", m + 1, r, k, clear, near);
            }
        }
    }
    // The objective with ground targets: the air quota ends the fighters, not the mission.
    game::Objective o;
    o.reset(5, 3);
    o.enemyDestroyed();
    o.enemyDestroyed();
    CHECK(o.airDone() && !o.allDestroyed(), "2 fighters of 2: air done, ships remain");
    o.groundTargetDestroyed();
    o.groundTargetDestroyed();
    CHECK(o.phase() == game::Objective::Phase::Playing, "a ship left: still playing");
    o.groundTargetDestroyed();
    CHECK(o.phase() == game::Objective::Phase::Won, "all down: won");
}

}  // namespace

int main() {
    testLayout();
    testNoHolesOnScreen();
    testRockTables();
    testIslandQueries();
    testRadarBehindIsland();
    testBroadPhase();
    testNorthSea();
    testBoundary();
    testShips();
    testMissionLayouts();
    if (g_failures) {
        std::printf("%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("all world tests passed\n");
    return 0;
}
