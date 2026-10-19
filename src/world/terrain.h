#pragma once

// The rocks and islands of a map (docs/decisions/0042): where they stand and
// what can't pass through them. Platform-neutral: the simulation tests
// aircraft, missiles, shells and every sensor's line of sight against it; the
// renderer draws the same placements with the meshes built from the same
// tables.

#include <cstdint>

#include "math/quat.h"
#include "math/vec3.h"
#include "world/rock_shape.h"

namespace world {

struct Rock {
    int shape = 0;           // index into kRockShapes
    float x = 0.0f, z = 0.0f;
    float yaw = 0.0f;        // rad about +Y: world = Quat::axisAngle(kUp, yaw).rotate(local)
    float cosYaw = 1.0f, sinYaw = 0.0f;
    math::Quat rot;          // Quat::axisAngle(kUp, yaw), computed once (no trig per frame)
};

class Terrain {
public:
    static constexpr int kMaxRocks = 128;

    void clear();
    void add(int shape, float x, float z, float yaw);

    int count() const { return count_; }
    const Rock& rock(int i) const { return rocks_[i]; }
    const RockShape& shape(int i) const;
    math::Quat orientation(int i) const;
    // Highest point of any rock: above it nothing can hit them.
    float maxHeight() const { return maxHeight_; }

    // Is the point inside a rock?
    bool solid(const math::Vec3& p) const;
    // First contact of the segment a -> b with a rock: its fraction 0..1
    // (to ~0.2 m), or -1 if the segment is clear. Walks each rock's grid of
    // column tops: cells it passes over are skipped, cells it passes through
    // below their solid floor are a hit, only the rest is marched.
    float segment(const math::Vec3& a, const math::Vec3& b) const;
    bool lineOfSight(const math::Vec3& a, const math::Vec3& b) const { return segment(a, b) < 0.0f; }
    // Height to keep above at (x, z) (0 over the open sea): the highest rock
    // within `margin` of the point (column-top grid, conservative) - for the
    // bots' terrain avoidance and safe spawns.
    float clearanceHeight(float x, float z, float margin) const;
    // Radius of a rock's outline at the waterline, by world azimuth (for the
    // surf and the radar scope): from a table built in add() - the surf and
    // the scope ask for hundreds of points a frame.
    float waterlineRadius(int rock, float theta) const;
    static constexpr int kWaterlineSamples = 384;

    // Broad phase: the rocks are listed in the cells of a coarse grid over the
    // map (each in every cell its widest outline touches), so a query looks
    // only at the rocks along its way - a 15 m shell step at one or two, not
    // all of them. Built on the first query after add()/clear().
    static constexpr float kCell = 1000.0f;       // m
    static constexpr int kGrid = 40;               // cells a side, centred on the origin (+-20 km)

private:
    bool insideRock(const Rock& r, const math::Vec3& p) const;
    float segmentRock(const Rock& r, const math::Vec3& a, const math::Vec3& b, float best) const;
    void buildGrid() const;
    bool firstVisit(int rock) const;  // this query has not looked at the rock yet
    Rock rocks_[kMaxRocks];
    mutable bool gridDirty_ = true;
    mutable uint16_t cellStart_[kGrid * kGrid + 1] = {};
    mutable uint8_t cellRocks_[kGrid * kGrid * 8] = {};  // rock indices, cell by cell
    mutable uint32_t visit_[kMaxRocks] = {};
    mutable uint32_t stamp_ = 0;
    float waterline_[kMaxRocks][kWaterlineSamples];  // by world azimuth 2 pi k / samples
    int count_ = 0;
    float maxHeight_ = 0.0f;
};

// Is (rho, y) at azimuth theta inside the shape (its own frame)?
bool shapeInside(const RockShape& s, float theta, float rho, float y);
// Radius of a shape's walls at (theta, y), y at or under the wall top there.
float shapeRadius(const RockShape& s, float theta, float y);
// Height of the cap at (theta, rho) - meaningful inside the top ring.
float shapeCapHeight(const RockShape& s, float theta, float rho);

// The map's rocks and its arena (docs/decisions/0042, 0043).
struct Arena {
    float centreX = 0.0f, centreZ = 0.0f;
    float warnRadius = 18000.0f;   // m: "turn back" beyond it
    float forceRadius = 20500.0f;  // m: the game turns the nose back beyond it
};

enum class MapId : uint8_t { NorthSea };
void buildMap(MapId map, Terrain& terrain, Arena& arena);

}  // namespace world
