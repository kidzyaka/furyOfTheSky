#include "world/terrain.h"

#include <cmath>

#include "world/rock_shapes.h"

namespace world {

namespace {

constexpr float kTwoPi = 6.28318531f;

struct Sector {
    int i0, i1;
    float f;
};

Sector sectorAt(const RockShape& s, float theta) {
    float t = theta * (1.0f / kTwoPi);
    t -= static_cast<float>(static_cast<int>(t));  // no std::floor: a libm call on the Allegrex
    if (t < 0.0f) t += 1.0f;
    const float si = t * static_cast<float>(s.sectors);
    int i0 = static_cast<int>(si);
    if (i0 >= s.sectors) i0 = s.sectors - 1;
    return {i0, (i0 + 1) % s.sectors, si - static_cast<float>(i0)};
}

// Sector i's wall radius at y: between the ring below and the next ring,
// that one clamped to the sector's wall top (as the mesh is built).
float sectorRadius(const RockShape& s, int i, float y) {
    float jf = (y - s.yBottom) / s.ringStep;
    if (jf < 0.0f) jf = 0.0f;
    int j0 = static_cast<int>(jf);
    if (j0 > s.rings - 2) j0 = s.rings - 2;
    const float y0 = s.yBottom + static_cast<float>(j0) * s.ringStep;
    float y1 = y0 + s.ringStep;
    if (y1 > s.wallTop[i]) y1 = s.wallTop[i];
    float t = (y - y0) / (y1 - y0 > 1e-3f ? y1 - y0 : 1e-3f);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float r0 = s.radius[j0 * s.sectors + i], r1 = s.radius[(j0 + 1) * s.sectors + i];
    return r0 + (r1 - r0) * t;
}

float wallRadius(const RockShape& s, const Sector& k, float y) {
    const float a = sectorRadius(s, k.i0, y), b = sectorRadius(s, k.i1, y);
    return a + (b - a) * k.f;
}

float edgeRadius(const RockShape& s, const Sector& k) {
    const float* top = s.radius + (s.rings - 1) * s.sectors;
    return top[k.i0] + (top[k.i1] - top[k.i0]) * k.f;
}

// Cap height at `frac` of the edge radius (1 at the edge, 0 at the centre).
float capHeight(const RockShape& s, const Sector& k, float frac) {
    const int n = s.sectors;
    int r = 0;
    while (r + 2 < s.capCount && frac < s.capRho[r + 1]) ++r;
    const float d = s.capRho[r] - s.capRho[r + 1];
    float t = (s.capRho[r] - frac) / (d > 1e-6f ? d : 1e-6f);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float* c0 = s.capY + r * n;
    const float* c1 = c0 + n;
    const float h0 = c0[k.i0] + (c0[k.i1] - c0[k.i0]) * k.f;
    const float h1 = c1[k.i0] + (c1[k.i1] - c1[k.i0]) * k.f;
    return h0 + (h1 - h0) * t;
}

bool insideLocal(const RockShape& s, const math::Vec3& p) {
    const float d2 = p.x * p.x + p.z * p.z;
    if (d2 > s.maxRadius * s.maxRadius) return false;
    return shapeInside(s, std::atan2(p.z, p.x), std::sqrt(d2), p.y);
}

}  // namespace

bool shapeInside(const RockShape& s, float theta, float rho, float y) {
    if (y > s.apex || y < s.yBottom || rho > s.maxRadius) return false;
    const Sector k = sectorAt(s, theta);
    const float wt = s.wallTop[k.i0] + (s.wallTop[k.i1] - s.wallTop[k.i0]) * k.f;
    const float edge = edgeRadius(s, k);
    // Under the wall top: inside the walls - and, within the top outline,
    // under the cap too (an island's ground can dip below its cliff tops).
    if (y <= wt) {
        if (rho >= wallRadius(s, k, y)) return false;
        return rho >= edge || y < capHeight(s, k, rho / edge);
    }
    if (rho >= edge) return false;
    return y < capHeight(s, k, rho / edge);
}

float shapeRadius(const RockShape& s, float theta, float y) { return wallRadius(s, sectorAt(s, theta), y); }

float shapeCapHeight(const RockShape& s, float theta, float rho) {
    const Sector k = sectorAt(s, theta);
    const float edge = edgeRadius(s, k);
    return capHeight(s, k, rho / (edge > 1e-3f ? edge : 1e-3f));
}

void Terrain::clear() {
    count_ = 0;
    maxHeight_ = 0.0f;
    gridDirty_ = true;
}

namespace {
constexpr float kGridHalf = Terrain::kCell * Terrain::kGrid * 0.5f;
inline int cellOfCoord(float v) {
    const float t = (v + kGridHalf) * (1.0f / Terrain::kCell);
    return t < 0.0f ? -1 : static_cast<int>(t);
}
}  // namespace

void Terrain::buildGrid() const {
    // Count, then fill (CSR). A rock goes into every cell its bounding circle overlaps.
    uint16_t count[kGrid * kGrid] = {};
    auto forCells = [&](const Rock& r, auto&& fn) {
        const float m = kRockShapes[r.shape].maxRadius;
        const int x0 = cellOfCoord(r.x - m), x1 = cellOfCoord(r.x + m);
        const int z0 = cellOfCoord(r.z - m), z1 = cellOfCoord(r.z + m);
        for (int cz = z0 < 0 ? 0 : z0; cz <= z1 && cz < kGrid; ++cz)
            for (int cx = x0 < 0 ? 0 : x0; cx <= x1 && cx < kGrid; ++cx) fn(cz * kGrid + cx);
    };
    for (int i = 0; i < count_; ++i) forCells(rocks_[i], [&](int c) { ++count[c]; });
    int total = 0;
    for (int c = 0; c < kGrid * kGrid; ++c) {
        cellStart_[c] = static_cast<uint16_t>(total);
        total += count[c];
    }
    cellStart_[kGrid * kGrid] = static_cast<uint16_t>(total);
    uint16_t fill[kGrid * kGrid] = {};
    for (int i = 0; i < count_; ++i)
        forCells(rocks_[i], [&](int c) {
            const int at = cellStart_[c] + fill[c]++;
            if (at < static_cast<int>(sizeof cellRocks_)) cellRocks_[at] = static_cast<uint8_t>(i);
        });
    gridDirty_ = false;
}

bool Terrain::firstVisit(int rock) const {
    if (visit_[rock] == stamp_) return false;
    visit_[rock] = stamp_;
    return true;
}

void Terrain::add(int shape, float x, float z, float yaw) {
    if (count_ >= kMaxRocks || shape < 0 || shape >= kRockShapeCount) return;
    Rock& r = rocks_[count_++];
    r.shape = shape;
    r.x = x;
    r.z = z;
    r.yaw = yaw;
    r.cosYaw = std::cos(yaw);
    r.sinYaw = std::sin(yaw);
    r.rot = math::Quat::axisAngle(math::kUp, yaw);
    gridDirty_ = true;
    if (kRockShapes[shape].apex > maxHeight_) maxHeight_ = kRockShapes[shape].apex;
    // World azimuth -> rock azimuth: the yaw turns the rock (theta = atan2(z, x)).
    for (int k = 0; k < kWaterlineSamples; ++k)
        waterline_[count_ - 1][k] = shapeRadius(kRockShapes[shape], kTwoPi * k / kWaterlineSamples + yaw, 0.0f);
}

const RockShape& Terrain::shape(int i) const { return kRockShapes[rocks_[i].shape]; }

math::Quat Terrain::orientation(int i) const { return rocks_[i].rot; }

bool Terrain::insideRock(const Rock& r, const math::Vec3& p) const {
    const RockShape& s = kRockShapes[r.shape];
    const float dx = p.x - r.x, dz = p.z - r.z;
    if (dx * dx + dz * dz > s.maxRadius * s.maxRadius || p.y > s.apex || p.y < s.yBottom) return false;
    // World -> rock frame: the inverse of the yaw about +Y (math::Quat convention).
    return insideLocal(s, {dx * r.cosYaw - dz * r.sinYaw, p.y, dx * r.sinYaw + dz * r.cosYaw});
}

bool Terrain::solid(const math::Vec3& p) const {
    if (p.y > maxHeight_ || count_ == 0) return false;
    if (gridDirty_) buildGrid();
    const int cx = cellOfCoord(p.x), cz = cellOfCoord(p.z);
    if (cx < 0 || cz < 0 || cx >= kGrid || cz >= kGrid) return false;
    const int c = cz * kGrid + cx;
    for (int k = cellStart_[c]; k < cellStart_[c + 1]; ++k)
        if (insideRock(rocks_[cellRocks_[k]], p)) return true;
    return false;
}

float Terrain::segmentRock(const Rock& r, const math::Vec3& a, const math::Vec3& b, float best) const {
    const RockShape& s = kRockShapes[r.shape];
    auto local = [&](const math::Vec3& p) {
        const float dx = p.x - r.x, dz = p.z - r.z;
        return math::Vec3{dx * r.cosYaw - dz * r.sinYaw, p.y, dx * r.sinYaw + dz * r.cosYaw};
    };
    const math::Vec3 la = local(a);
    const math::Vec3 d = local(b) - la;
    const float len = math::length(d);
    // Where the segment is inside the bounding cylinder (x/z circle) ...
    const float A = d.x * d.x + d.z * d.z;
    const float B = 2.0f * (la.x * d.x + la.z * d.z);
    const float C = la.x * la.x + la.z * la.z - s.maxRadius * s.maxRadius;
    float t0, t1;
    if (A < 1e-8f) {
        if (C > 0.0f) return best;
        t0 = 0.0f;
        t1 = 1.0f;
    } else {
        const float disc = B * B - 4.0f * A * C;
        if (disc < 0.0f) return best;
        const float q = std::sqrt(disc);
        t0 = (-B - q) / (2.0f * A);
        t1 = (-B + q) / (2.0f * A);
    }
    if (t0 < 0.0f) t0 = 0.0f;
    if (t1 > 1.0f) t1 = 1.0f;
    if (t0 >= t1 || t0 >= best) return best;
    // ... and below the apex.
    if (la.y + d.y * t0 > s.apex && la.y + d.y * t1 > s.apex) return best;

    // Walk the column-top grid cell by cell (Amanatides-Woo).
    const float cell = s.gridCell;
    const int w = s.gridW;
    const float gx = (la.x + d.x * t0 - s.gridX0) / cell, gz = (la.z + d.z * t0 - s.gridX0) / cell;
    int cx = static_cast<int>(gx), cz = static_cast<int>(gz);
    constexpr float kFar = 1e30f;
    const int sx = d.x > 0.0f ? 1 : -1, sz = d.z > 0.0f ? 1 : -1;
    const float ddx = std::fabs(d.x) > 1e-9f ? cell / std::fabs(d.x) : kFar;
    const float ddz = std::fabs(d.z) > 1e-9f ? cell / std::fabs(d.z) : kFar;
    float nx = ddx >= kFar ? kFar : t0 + (d.x > 0.0f ? (cx + 1 - gx) : (gx - cx)) * ddx;
    float nz = ddz >= kFar ? kFar : t0 + (d.z > 0.0f ? (cz + 1 - gz) : (gz - cz)) * ddz;
    float step = s.maxRadius * 0.08f;
    step = (step < 1.0f ? 1.0f : (step > 4.0f ? 4.0f : step)) / len;
    float t = t0;
    while (t < t1 && t < best) {
        const float tn = std::fmin(std::fmin(nx, nz), t1);
        if (cx >= 0 && cx < w && cz >= 0 && cz < w) {
            const int k = cz * w + cx;
            const int16_t top = s.topMax[k];
            if (top != kNoRock) {
                const float ya = la.y + d.y * t, yb = la.y + d.y * tn;
                if (std::fmin(ya, yb) <= top) {
                    const int16_t floor = s.topMin[k];
                    // Through the cell under its solid floor (and above the
                    // walls' foot): in rock from its entry.
                    if (floor != kNoRock && std::fmax(ya, yb) < floor && std::fmin(ya, yb) >= s.yBottom) return t;
                    float prev = t;
                    for (float tt = t;; tt += step) {
                        const float tc = tt > tn ? tn : tt;
                        if (insideLocal(s, la + d * tc)) {
                            float lo = prev, hi = tc;
                            for (int it = 0; it < 6 && (hi - lo) * len > 0.2f; ++it) {
                                const float mid = 0.5f * (lo + hi);
                                if (insideLocal(s, la + d * mid)) hi = mid; else lo = mid;
                            }
                            return hi;
                        }
                        prev = tc;
                        if (tc >= tn) break;
                    }
                }
            }
        }
        if (nx < nz) {
            cx += sx;
            t = nx;
            nx += ddx;
        } else {
            cz += sz;
            t = nz;
            nz += ddz;
        }
    }
    return best;
}

float Terrain::segment(const math::Vec3& a, const math::Vec3& b) const {
    if (count_ == 0 || (a.y > maxHeight_ && b.y > maxHeight_)) return -1.0f;
    const math::Vec3 d = b - a;
    if (d.x * d.x + d.y * d.y + d.z * d.z < 1e-8f) return solid(a) ? 0.0f : -1.0f;
    if (gridDirty_) buildGrid();
    ++stamp_;
    const float ylo = a.y < b.y ? a.y : b.y;
    // Walk the broad-phase cells along the segment (x/z), nearest first; a
    // hit found in a cell is final once the next cell starts beyond it.
    const float gx = (a.x + kGridHalf) / kCell, gz = (a.z + kGridHalf) / kCell;
    const float ex = (b.x + kGridHalf) / kCell, ez = (b.z + kGridHalf) / kCell;
    // Clip the walk to the grid: the parameter range inside [0, kGrid]^2.
    float t0 = 0.0f, t1 = 1.0f;
    auto clip = [&](float p, float q) {  // p * t <= q
        if (std::fabs(p) < 1e-12f) return q >= 0.0f;
        const float r = q / p;
        if (p < 0.0f) { if (r > t0) t0 = r; } else if (r < t1) t1 = r;
        return true;
    };
    const float dgx = ex - gx, dgz = ez - gz;
    if (!clip(-dgx, gx) || !clip(dgx, kGrid - gx) || !clip(-dgz, gz) || !clip(dgz, kGrid - gz) || t0 > t1)
        return -1.0f;
    float px = gx + dgx * t0, pz = gz + dgz * t0;
    int cx = static_cast<int>(px), cz = static_cast<int>(pz);
    cx = cx < 0 ? 0 : (cx >= kGrid ? kGrid - 1 : cx);
    cz = cz < 0 ? 0 : (cz >= kGrid ? kGrid - 1 : cz);
    constexpr float kFar = 1e30f;
    const int sx = dgx > 0.0f ? 1 : -1, sz = dgz > 0.0f ? 1 : -1;
    const float ddx = std::fabs(dgx) > 1e-9f ? 1.0f / std::fabs(dgx) : kFar;
    const float ddz = std::fabs(dgz) > 1e-9f ? 1.0f / std::fabs(dgz) : kFar;
    float nx = ddx >= kFar ? kFar : t0 + (dgx > 0.0f ? (cx + 1 - px) : (px - cx)) * ddx;
    float nz = ddz >= kFar ? kFar : t0 + (dgz > 0.0f ? (cz + 1 - pz) : (pz - cz)) * ddz;
    float best = 2.0f, tCell = t0;
    while (tCell <= t1 && tCell < best) {
        const int c = cz * kGrid + cx;
        for (int k = cellStart_[c]; k < cellStart_[c + 1]; ++k) {
            const int i = cellRocks_[k];
            if (!firstVisit(i)) continue;
            if (ylo > kRockShapes[rocks_[i].shape].apex) continue;  // passes over it
            best = segmentRock(rocks_[i], a, b, best);
        }
        if (nx < nz) {
            cx += sx;
            tCell = nx;
            nx += ddx;
        } else {
            cz += sz;
            tCell = nz;
            nz += ddz;
        }
        if (cx < 0 || cz < 0 || cx >= kGrid || cz >= kGrid) break;
    }
    return best <= 1.0f ? best : -1.0f;
}

float Terrain::clearanceHeight(float x, float z, float margin) const {
    float h = 0.0f;
    if (count_ == 0) return h;
    if (gridDirty_) buildGrid();
    ++stamp_;
    const int gx0 = cellOfCoord(x - margin), gx1 = cellOfCoord(x + margin);
    const int gz0 = cellOfCoord(z - margin), gz1 = cellOfCoord(z + margin);
    for (int gz = gz0 < 0 ? 0 : gz0; gz <= gz1 && gz < kGrid; ++gz)
    for (int gx = gx0 < 0 ? 0 : gx0; gx <= gx1 && gx < kGrid; ++gx)
    for (int k = cellStart_[gz * kGrid + gx]; k < cellStart_[gz * kGrid + gx + 1]; ++k) {
        const int i = cellRocks_[k];
        if (!firstVisit(i)) continue;
        const Rock& r = rocks_[i];
        const RockShape& s = kRockShapes[r.shape];
        const float dx = x - r.x, dz = z - r.z;
        const float reach = s.maxRadius + margin;
        if (dx * dx + dz * dz > reach * reach || s.apex <= h) continue;
        const float lx = dx * r.cosYaw - dz * r.sinYaw, lz = dx * r.sinYaw + dz * r.cosYaw;
        auto cellOf = [&](float v) {
            const int c = static_cast<int>((v - s.gridX0) / s.gridCell + 1.0f) - 1;  // floor for v >= -cell
            return c < 0 ? 0 : (c >= s.gridW ? s.gridW - 1 : c);
        };
        const int x0 = cellOf(lx - margin), x1 = cellOf(lx + margin);
        const int z0 = cellOf(lz - margin), z1 = cellOf(lz + margin);
        for (int cz = z0; cz <= z1; ++cz)
            for (int cx = x0; cx <= x1; ++cx) {
                const int16_t top = s.topMax[cz * s.gridW + cx];
                if (top != kNoRock && top > h) h = top;
            }
    }
    return h;
}

float Terrain::waterlineRadius(int rock, float theta) const {
    float t = theta * (kWaterlineSamples / kTwoPi);
    int k = static_cast<int>(t);
    if (t < 0.0f) --k;  // floor without libm (gotchas #30)
    const float f = t - static_cast<float>(k);
    k %= kWaterlineSamples;
    if (k < 0) k += kWaterlineSamples;
    const float* w = waterline_[rock];
    const float a = w[k], b = w[(k + 1) % kWaterlineSamples];
    return a + (b - a) * f;
}

// --- Maps. ----------------------------------------------------------------

namespace {

int shapeIndex(const char* name) {
    for (int i = 0; i < kRockShapeCount; ++i) {
        const char* a = kRockShapes[i].name;
        const char* b = name;
        while (*a && *a == *b) ++a, ++b;
        if (*a == 0 && *b == 0) return i;
    }
    return 0;
}

struct Placed {
    const char* shape;
    float x, z;  // m
    float yaw;   // rad; < -9: from the map's fixed LCG
};

constexpr float R = -10.0f;  // "any yaw"

}  // namespace

// The North Sea archipelago (docs/assets/rocks.md): -z is north, the arena
// 18 km round; the missions come in from the south ~11.5 km out with the
// first islands 2-3 km ahead, the training start is the centre at 900 m.
// Islands every 3-5 km so cover is always a short dash away: Conachair
// (Hirta-like, cliffs north) and the ridge of Mykines with the Sound between
// them (1.4 km of water under cliffs); Foula east of the centre, cliffs
// facing it; the Eysturoy ridge south-west; two holms near the middle; a ring
// of Boreray, Soay, Stroma and their twins (turned) further out; stack
// gardens and single stacks in every gap.
static const Placed kNorthSea[] = {
    // The core.
    {"conachair", -2600.0f, -2700.0f, 0.0f},
    {"mykines", 600.0f, -5400.0f, 0.3f},
    {"foula", 4300.0f, 800.0f, -0.25f},
    {"eysturoy", -4200.0f, 3400.0f, 1.3f},
    {"holm", 1500.0f, -1500.0f, R},
    {"dun", -500.0f, 3000.0f, R},
    // The ring.
    {"boreray", -7600.0f, -6200.0f, 0.4f},
    {"soay", -8300.0f, -1000.0f, R},
    {"stroma", 6600.0f, -6600.0f, R},
    {"dun", 8200.0f, -1800.0f, R},
    {"holm", 2400.0f, 6500.0f, R},
    {"boreray", 7200.0f, 5400.0f, 2.2f},
    {"soay", -1500.0f, 8600.0f, R},
    {"stroma", -7500.0f, 7600.0f, R},
    {"islet", 10500.0f, 2500.0f, R}, {"islet", -10800.0f, 3600.0f, R},
    {"islet", 2800.0f, -10600.0f, R}, {"islet", -4500.0f, -9800.0f, R},
    // The stack garden between Conachair and Boreray.
    {"armin", -5200.0f, -4800.0f, R}, {"tower", -6000.0f, -4500.0f, R}, {"needle", -4700.0f, -5600.0f, R},
    {"hoy", -5600.0f, -5800.0f, R}, {"needle", -6500.0f, -5300.0f, R}, {"tower", -5000.0f, -6500.0f, R},
    {"hoy", -6200.0f, -6600.0f, R}, {"skerry", -4300.0f, -4800.0f, R}, {"armin", -6900.0f, -4200.0f, R},
    // At the mouths of the Sound.
    {"hoy", -2900.0f, -5300.0f, R}, {"tower", 900.0f, -3600.0f, R}, {"skerry", -300.0f, -3400.0f, R},
    // Round the centre: cover for the training fight.
    {"hoy", -1700.0f, 1100.0f, R}, {"needle", 1300.0f, 1700.0f, R}, {"tower", -900.0f, -600.0f, R},
    // The southern approach.
    {"tower", -3000.0f, 6500.0f, R}, {"needle", 1000.0f, 4600.0f, R}, {"hoy", 4600.0f, 4200.0f, R},
    {"armin", 800.0f, 9600.0f, R}, {"skerry", -2600.0f, 9800.0f, R}, {"needle", 3800.0f, 9200.0f, R},
    {"tower", -4400.0f, 9400.0f, R}, {"hoy", 5800.0f, 8600.0f, R},
    // North.
    {"armin", 600.0f, -9300.0f, R}, {"hoy", -1600.0f, -9900.0f, R}, {"needle", 4300.0f, -9500.0f, R},
    {"tower", 5300.0f, -11300.0f, R}, {"skerry", 2100.0f, -8200.0f, R},
    // West.
    {"needle", -9800.0f, -3700.0f, R}, {"tower", -10600.0f, 600.0f, R}, {"hoy", -9400.0f, 5200.0f, R},
    {"skerry", -7000.0f, 1800.0f, R},
    // East.
    {"tower", 10200.0f, -4600.0f, R}, {"needle", 9700.0f, -6900.0f, R}, {"hoy", 10600.0f, 300.0f, R},
    {"skerry", 6000.0f, -3800.0f, R},
    // Filling the gaps.
    {"armin", 5000.0f, -4600.0f, R}, {"tower", 4200.0f, -7200.0f, R}, {"hoy", -8800.0f, -8200.0f, R},
    {"needle", 8500.0f, 7800.0f, R}, {"tower", -7600.0f, 10000.0f, R}, {"skerry", 3600.0f, -2600.0f, R},
    {"needle", -3600.0f, -7400.0f, R},
};

namespace {

}  // namespace

void buildMap(MapId map, Terrain& terrain, Arena& arena) {
    terrain.clear();
    arena = Arena{};
    (void)map;  // one map so far: the North Sea
    // Yaws from a fixed LCG: the map is the same on every boot and every peer.
    uint32_t rng = 0x5EA57ACu;
    for (const Placed& p : kNorthSea) {
        rng = rng * 1664525u + 1013904223u;
        const float yaw = p.yaw < -9.0f ? static_cast<float>(rng >> 8) * (kTwoPi / 16777216.0f) : p.yaw;
        terrain.add(shapeIndex(p.shape), p.x, p.z, yaw);
    }
}

}  // namespace world
