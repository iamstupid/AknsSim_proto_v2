#include "sim_core/components/block.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

#include "sim_core/components/destroyed.hpp"
#include "sim_core/components/position.hpp"
#include "sim_core/components/route_move.hpp"
#include "sim_core/components/spatial.hpp"
#include "sim_core/core/movement_utils.hpp"
#include "sim_core/spatial/spatial_grid.hpp"

namespace arksim {
namespace {

constexpr f32 kEps = 0.00001f;
constexpr f32 kMinSeparation = 0.5f;
constexpr f32 kEnemyCoreRadius = 0.1f;
constexpr f32 kSoftPushRange = 0.4f;
constexpr f32 kCorrectionFinalLen = 0.2f;
constexpr f32 kSoftPushScale = 50.0f;

struct Candidate {
  double dist2 = 0.0;
  Entity enemy{};
  vec<f32> center{};
};

struct StablePos {
  std::uint64_t id = 0;
  vec<f32> pos{};
};

inline bool timer_due(Tick& remain, Tick interval, Tick tick_rate) {
  if (interval == 0) {
    return true;
  }
  if (remain == 0 || remain <= tick_rate) {
    remain = interval;
    return true;
  }
  remain -= tick_rate;
  return false;
}

inline vec<f32> rotate90_cw(const vec<f32>& v) {
  // x right, y up.
  return vec<f32>{v.y, -v.x};
}

inline vec<f32> set_length_or_zero(const vec<f32>& v, f32 len) {
  if (!(len > 0.0f)) {
    return vec<f32>{};
  }
  const vec<f32> n = v.normalized();
  if (!(n.length_sq() > 0.0f)) {
    return vec<f32>{};
  }
  return n * len;
}

vec<f32> compute_stable_block_pos(const vec<f32>& a,
                                  const vec<f32>& b,
                                  const std::vector<StablePos>& existing) {
  const vec<f32> ab = b - a;
  const f32 ab_len = ab.length();
  if (!(ab_len > kEps)) {
    // Centers overlap (or almost overlap): no offset.
    return b;
  }

  vec<f32> ac = ab;
  if (ab_len < kMinSeparation) {
    ac = set_length_or_zero(ab, kMinSeparation);
    if (!(ac.length_sq() > 0.0f)) {
      return b;
    }
  }

  const vec<f32> c = a + ac;

  bool conflict = false;
  for (const StablePos& d : existing) {
    if ((d.pos - c).length() <= kEnemyCoreRadius) {
      conflict = true;
      break;
    }
  }

  if (!conflict) {
    return c;
  }

  vec<f32> corr{};
  for (const StablePos& d : existing) {
    const vec<f32> dnc = c - d.pos;
    const f32 dlen = dnc.length();

    if (dlen < kEnemyCoreRadius) {
      const vec<f32> adn = d.pos - a;
      const vec<f32> rot = rotate90_cw(adn);
      corr += rot.normalized();
    } else if (dlen < kSoftPushRange) {
      corr += set_length_or_zero(dnc, (kSoftPushRange - dlen) * kSoftPushScale);
    }
  }

  if (corr.length() > kEps) {
    corr = set_length_or_zero(corr, kCorrectionFinalLen);
  } else {
    corr = vec<f32>{};
  }

  vec<f32> ae = ac + corr;
  if (!(ae.length() > kEps)) {
    ae = ac;
  }

  const f32 ac_target_len = ac.length();
  const vec<f32> af = set_length_or_zero(ae, ac_target_len);
  if (!(af.length_sq() > 0.0f)) {
    return c;
  }
  return a + af;
}

void clear_block_flags(World& world, Entity enemy) {
  if (auto* spatial = world.try_get<Spatial>(enemy)) {
    spatial->flags &= ~TypeFlags::Blocked;
  }
}

void ensure_block_flags(World& world, Entity enemy) {
  if (auto* spatial = world.try_get<Spatial>(enemy)) {
    spatial->flags |= TypeFlags::Blocked;
  }
}

void stop_enemy_motion(World& world, Entity enemy) {
  if (auto* pos = world.try_get<Position>(enemy)) {
    set_position_velocity(*pos, vec<f32>{});
    pos->target_speed = 0.0f;
    pos->accel_speed = 0.0f;
  }
}

void start_forced_move(RouteMove& rm, const vec<f32>& start, const vec<f32>& target) {
  rm.forced_move.active = true;
  rm.forced_move.start = start;
  rm.forced_move.target = target;
  rm.forced_move.duration = Blocker::kForcedMoveDuration;
  rm.forced_move.elapsed = 0;
}

void update_forced_move(RouteMove& rm, Position& pos, Tick tick_rate) {
  if (!rm.forced_move.active) {
    return;
  }

  const Tick dur = rm.forced_move.duration;
  if (dur == 0) {
    pos.pos = rm.forced_move.target;
    rm.forced_move.active = false;
    return;
  }

  const Tick next_elapsed = (rm.forced_move.elapsed > std::numeric_limits<Tick>::max() - tick_rate)
                                ? std::numeric_limits<Tick>::max()
                                : (rm.forced_move.elapsed + tick_rate);
  rm.forced_move.elapsed = next_elapsed;

  if (rm.forced_move.elapsed >= dur) {
    pos.pos = rm.forced_move.target;
    rm.forced_move.active = false;
    return;
  }

  const double t = static_cast<double>(rm.forced_move.elapsed) / static_cast<double>(dur);
  pos.pos = rm.forced_move.start + (rm.forced_move.target - rm.forced_move.start) * static_cast<f32>(t);
}

bool is_enemy_in_range(const vec<f32>& a, const vec<f32>& b, f32 radius) {
  if (!(radius > 0.0f)) {
    return false;
  }
  const vec<f32> d = b - a;
  return d.length_sq() <= radius * radius;
}

void stable_positions_sorted(World& world, Entity self, const std::vector<Entity>& blocked, std::vector<StablePos>& out) {
  out.clear();
  out.reserve(blocked.size());
  for (Entity e : blocked) {
    if (!world.is_alive(e)) {
      continue;
    }
    const auto* rm = world.try_get<RouteMove>(e);
    if (!rm) {
      continue;
    }
    if (rm->blocked_by.entity_id != self.entity_id) {
      continue;
    }
    out.push_back(StablePos{e.entity_id, rm->stable_block_pos});
  }

  std::sort(out.begin(), out.end(), [](const StablePos& a, const StablePos& b) { return a.id < b.id; });
}

void insert_stable_sorted(std::vector<StablePos>& v, StablePos s) {
  auto it = std::lower_bound(v.begin(), v.end(), s.id, [](const StablePos& a, std::uint64_t id) { return a.id < id; });
  v.insert(it, s);
}

} // namespace

void Blocker::step(World& world, Entity self, const SpatialIndex& spatial, Tick tick_rate) {
  if (!active) {
    return;
  }
  if (!world.is_alive(self) || world.has<Destroyed>(self)) {
    return;
  }

  const auto* self_pos = world.try_get<Position>(self);
  if (!self_pos) {
    return;
  }
  const vec<f32> a_center = self_pos->pos;

  // 1) Maintain existing blocked enemies (forced move + release invalid/out-of-range).
  bool released_any = false;
  std::vector<Entity> next_blocked;
  next_blocked.reserve(blocked.size());
  for (Entity enemy : blocked) {
    if (!world.is_alive(enemy) || world.has<Destroyed>(enemy)) {
      released_any = true;
      clear_block_flags(world, enemy);
      continue;
    }

    auto* rm = world.try_get<RouteMove>(enemy);
    auto* pos = world.try_get<Position>(enemy);
    if (!rm || !pos) {
      released_any = true;
      clear_block_flags(world, enemy);
      continue;
    }

    // If the enemy is no longer blocked by us (e.g. reassigned/cleared), drop it from our list.
    if (rm->blocked_by.entity_id != self.entity_id) {
      continue;
    }

    if (!is_enemy_in_range(a_center, pos->pos, block_radius)) {
      rm->clear_block();
      clear_block_flags(world, enemy);
      released_any = true;
      continue;
    }

    // Keep it bound while blocked.
    rm->is_bound = true;

    update_forced_move(*rm, *pos, tick_rate);
    stop_enemy_motion(world, enemy);
    ensure_block_flags(world, enemy);

    next_blocked.push_back(enemy);
  }

  blocked.swap(next_blocked);
  std::sort(blocked.begin(), blocked.end(), [](Entity a, Entity b) { return a.entity_id < b.entity_id; });

  if (released_any) {
    // More responsive: if capacity opens up, scan immediately next tick.
    scan_remain = 0;
  }

  // 2) Find new enemies to block (using previous-frame spatial index).
  if (blocked.size() >= block_capacity) {
    return;
  }

  const bool do_scan = timer_due(scan_remain, scan_interval, tick_rate);
  if (!do_scan) {
    return;
  }

  std::vector<StablePos> stable;
  stable_positions_sorted(world, self, blocked, stable);

  std::vector<Candidate> candidates;
  candidates.reserve(32);

  const TypeFlags required = TypeFlags::Enemy | TypeFlags::Blockable;
  spatial.center.query_circle(a_center, block_radius, required, [&](const SpatialEntry& e) {
    // Ignore already blocked targets.
    if (has_flags(e.flags, TypeFlags::Blocked)) {
      return;
    }
    if (!world.is_alive(e.entity) || world.has<Destroyed>(e.entity)) {
      return;
    }

    auto* rm = world.try_get<RouteMove>(e.entity);
    if (!rm) {
      return;
    }
    if (rm->is_blocked()) {
      return;
    }

    // Broadphase uses previous-frame spatial; validate using the current Position for correctness.
    const auto* pos = world.try_get<Position>(e.entity);
    if (!pos) {
      return;
    }

    // Eligibility is based on center-to-center distance only (A.center, B.center).
    if (!is_enemy_in_range(a_center, pos->pos, block_radius)) {
      return;
    }

    const double dx = static_cast<double>(pos->pos.x) - static_cast<double>(a_center.x);
    const double dy = static_cast<double>(pos->pos.y) - static_cast<double>(a_center.y);
    candidates.push_back(Candidate{dx * dx + dy * dy, e.entity, pos->pos});
  });

  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    if (a.dist2 != b.dist2) {
      return a.dist2 < b.dist2;
    }
    return a.enemy.entity_id < b.enemy.entity_id;
  });

  for (const Candidate& c : candidates) {
    if (blocked.size() >= block_capacity) {
      break;
    }

    if (!world.is_alive(c.enemy) || world.has<Destroyed>(c.enemy)) {
      continue;
    }

    auto* rm = world.try_get<RouteMove>(c.enemy);
    auto* pos = world.try_get<Position>(c.enemy);
    if (!rm || !pos) {
      continue;
    }
    if (rm->is_blocked()) {
      continue;
    }

    // Compute stable position based on already blocked enemies (stable positions).
    const vec<f32> stable_pos = compute_stable_block_pos(a_center, pos->pos, stable);

    rm->blocked_by = self;
    rm->stable_block_pos = stable_pos;
    rm->is_bound = true;
    start_forced_move(*rm, pos->pos, stable_pos);
    stop_enemy_motion(world, c.enemy);
    ensure_block_flags(world, c.enemy);

    blocked.push_back(c.enemy);
    insert_stable_sorted(stable, StablePos{c.enemy.entity_id, stable_pos});
  }

  std::sort(blocked.begin(), blocked.end(), [](Entity a, Entity b) { return a.entity_id < b.entity_id; });
}

} // namespace arksim
