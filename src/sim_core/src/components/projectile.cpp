#include "sim_core/components/projectile.hpp"

#include <algorithm>
#include <cmath>

#include "sim_core/components/attack_power.hpp"
#include "sim_core/components/area.hpp"
#include "sim_core/components/destroyed.hpp"
#include "sim_core/components/position.hpp"
#include "sim_core/destroyed.hpp"
#include "sim_core/effects.hpp"
#include "sim_core/sim_state.hpp"
#include "sim_core/spatial_grid.hpp"

namespace arksim {
namespace {

inline f32 area_bound_radius(const Area& area) {
  if (area.type == Area::Type::Circle) {
    return std::max(area.radius.x, 0.0f);
  }
  const double w = std::max(0.0, static_cast<double>(area.radius.x));
  const double h = std::max(0.0, static_cast<double>(area.radius.y));
  const double hx = w * 0.5;
  const double hy = h * 0.5;
  return static_cast<f32>(std::sqrt(hx * hx + hy * hy));
}

bool circle_intersects_rect(const vec<f32>& cc, f32 cr, const vec<f32>& rc, f32 w, f32 h) {
  const f32 hx = w * 0.5f;
  const f32 hy = h * 0.5f;
  const f32 dx = std::abs(cc.x - rc.x) - hx;
  const f32 dy = std::abs(cc.y - rc.y) - hy;
  const f32 nx = std::max(dx, 0.0f);
  const f32 ny = std::max(dy, 0.0f);
  return nx * nx + ny * ny <= cr * cr;
}

bool rect_intersects_rect(const vec<f32>& ac, f32 aw, f32 ah, const vec<f32>& bc, f32 bw, f32 bh) {
  const f32 ahx = aw * 0.5f;
  const f32 ahy = ah * 0.5f;
  const f32 bhx = bw * 0.5f;
  const f32 bhy = bh * 0.5f;
  return std::abs(ac.x - bc.x) <= (ahx + bhx) && std::abs(ac.y - bc.y) <= (ahy + bhy);
}

bool intersects_area(const vec<f32>& a_center, const Area& a, const vec<f32>& b_center, const Area& b) {
  if (a.type == Area::Type::Circle && b.type == Area::Type::Circle) {
    const f32 ar = std::max(a.radius.x, 0.0f);
    const f32 br = std::max(b.radius.x, 0.0f);
    const vec<f32> d = a_center - b_center;
    const f32 rr = ar + br;
    return d.length_sq() <= rr * rr;
  }

  if (a.type == Area::Type::Circle && b.type == Area::Type::Rectangle) {
    const f32 ar = std::max(a.radius.x, 0.0f);
    const f32 bw = std::max(b.radius.x, 0.0f);
    const f32 bh = std::max(b.radius.y, 0.0f);
    return circle_intersects_rect(a_center, ar, b_center, bw, bh);
  }

  if (a.type == Area::Type::Rectangle && b.type == Area::Type::Circle) {
    return intersects_area(b_center, b, a_center, a);
  }

  // Rect-rect.
  const f32 aw = std::max(a.radius.x, 0.0f);
  const f32 ah = std::max(a.radius.y, 0.0f);
  const f32 bw = std::max(b.radius.x, 0.0f);
  const f32 bh = std::max(b.radius.y, 0.0f);
  return rect_intersects_rect(a_center, aw, ah, b_center, bw, bh);
}

bool circle_intersects_area(const vec<f32>& c, f32 r, const vec<f32>& a_center, const Area& a) {
  Area circle;
  circle.type = Area::Type::Circle;
  circle.radius = vec<f32>{r, 0.0f};
  return intersects_area(c, circle, a_center, a);
}

inline Tick tick_sub_sat(Tick a, Tick b) {
  if (a <= b) {
    return 0;
  }
  return a - b;
}

double read_attack_power_idx_gen(World& world, std::uint32_t idx, std::uint32_t gen) {
  const auto* ap = world.try_get_idx_gen<AttackPower>(idx, gen);
  if (!ap) {
    return 0.0;
  }
  return static_cast<double>(ap->atk);
}

} // namespace

void Projectile::settle_attack_power_snapshot(World& world) {
  has_attack_power_snapshot = true;
  attack_power_snapshot = 0.0;
  if (source_idx == ecs_lab::kInvalidIndex || source_gen == 0) {
    return;
  }
  attack_power_snapshot = read_attack_power_idx_gen(world, source_idx, source_gen);
}

void Projectile::set_source(Entity source) {
  source_idx = source.entity_idx;
  source_gen = source.gen;
}

void Projectile::clear_target() {
  target_idx = ecs_lab::kInvalidIndex;
  target_gen = 0;
}

void Projectile::set_target(Entity target) {
  target_idx = target.entity_idx;
  target_gen = target.gen;
}

void Projectile::step(World& world, SimState& sim, Entity self, const SpatialIndex& spatial, Tick tick_rate) {
  if (!active) {
    return;
  }
  if (world.has<Destroyed>(self)) {
    return;
  }

  if (use_source_attack_power && attack_power_settlement == AttackPowerSettlement::Snapshot && !has_attack_power_snapshot) {
    settle_attack_power_snapshot(world);
  }

  // Movement (if the entity owns a Position).
  if (auto* pos = world.try_get<Position>(self)) {
    pos->step(world, tick_rate);
  }

  // Interval gate.
  bool trigger = false;
  if (interval == 0) {
    trigger = true;
  } else if (interval_remain == 0) {
    interval_remain = interval;
  }

  if (interval != 0) {
    if (interval_remain <= tick_rate) {
      trigger = true;
      interval_remain = interval;
    } else {
      interval_remain = tick_sub_sat(interval_remain, tick_rate);
    }
  }

  if (!trigger) {
    return;
  }

  const auto* proj_pos = world.try_get<Position>(self);
  const auto* proj_area = world.try_get<Area>(self);
  if (!proj_pos || !proj_area) {
    return;
  }

  const vec<f32> proj_center = proj_pos->pos;
  const f32 proj_bound = area_bound_radius(*proj_area);

  std::vector<SpatialEntry> candidates;

  auto collect_overlapping = [&](std::vector<Entity>& out) {
    out.clear();
    candidates.clear();
    spatial.occupation.collect_circle_intersect(proj_center, proj_bound, required, candidates);
    for (const SpatialEntry& e : candidates) {
      if (e.entity.entity_idx == self.entity_idx && e.entity.gen == self.gen) {
        continue;
      }
      const auto* tgt_pos = world.try_get<Position>(e.entity);
      const auto* tgt_area = world.try_get<Area>(e.entity);
      if (!tgt_pos || !tgt_area) {
        continue;
      }
      if (intersects_area(proj_center, *proj_area, tgt_pos->pos, *tgt_area)) {
        out.push_back(e.entity);
      }
    }
    std::sort(out.begin(), out.end(), [](Entity a, Entity b) { return a.entity_id < b.entity_id; });
    out.erase(std::unique(out.begin(), out.end(), [](Entity a, Entity b) { return a.entity_id == b.entity_id; }),
              out.end());
  };

  std::vector<Entity> hit_targets;

  if (target_idx != ecs_lab::kInvalidIndex && target_gen != 0) {
    const Entity t = world.resolve_idx_gen(target_idx, target_gen);
    if (t.entity_id != 0) {
      const auto* tgt_pos = world.try_get<Position>(t);
      const auto* tgt_area = world.try_get<Area>(t);
      if (tgt_pos && tgt_area && intersects_area(proj_center, *proj_area, tgt_pos->pos, *tgt_area)) {
        hit_targets.push_back(t);
      }
    }
  } else {
    collect_overlapping(hit_targets);
    if (hit_shape == HitShape::SingleTarget && hit_targets.size() > 1) {
      hit_targets.resize(1);
    }
  }

  if (hit_targets.empty()) {
    return;
  }

  Entity src{};
  if (source_idx != ecs_lab::kInvalidIndex && source_gen != 0) {
    src.entity_idx = source_idx;
    src.gen = source_gen;
  }

  auto compute_damage_amount = [&]() -> double {
    double amount = dmg.amount;
    if (!use_source_attack_power) {
      return amount;
    }

    double atk = 0.0;
    switch (attack_power_settlement) {
      case AttackPowerSettlement::Snapshot:
        atk = has_attack_power_snapshot ? attack_power_snapshot : read_attack_power_idx_gen(world, source_idx, source_gen);
        break;
      case AttackPowerSettlement::BindToSource:
        atk = read_attack_power_idx_gen(world, source_idx, source_gen);
        break;
      default:
        break;
    }

    amount += atk_multiplier * atk;
    return amount;
  };

  if (hit_shape == HitShape::AoeCircle) {
    const f32 r = std::max(aoe_radius, 0.0f);
    std::vector<SpatialEntry> aoe;
    spatial.occupation.collect_circle_intersect(proj_center, r, required, aoe);

    hit_targets.clear();
    hit_targets.reserve(aoe.size());
    for (const SpatialEntry& e : aoe) {
      const auto* tgt_pos = world.try_get<Position>(e.entity);
      const auto* tgt_area = world.try_get<Area>(e.entity);
      if (!tgt_pos || !tgt_area) {
        continue;
      }
      if (!circle_intersects_area(proj_center, r, tgt_pos->pos, *tgt_area)) {
        continue;
      }
      hit_targets.push_back(e.entity);
    }

    std::sort(hit_targets.begin(), hit_targets.end(), [](Entity a, Entity b) { return a.entity_id < b.entity_id; });
    hit_targets.erase(
        std::unique(hit_targets.begin(), hit_targets.end(), [](Entity a, Entity b) { return a.entity_id == b.entity_id; }),
        hit_targets.end());
  } else if (hit_shape == HitShape::AoeTiles) {
    std::vector<SpatialEntry> aoe;
    spatial.occupation.collect_tiles(aoe_tiles, required, aoe);
    hit_targets.clear();
    hit_targets.reserve(aoe.size());
    for (const SpatialEntry& e : aoe) {
      hit_targets.push_back(e.entity);
    }
    std::sort(hit_targets.begin(), hit_targets.end(), [](Entity a, Entity b) { return a.entity_id < b.entity_id; });
    hit_targets.erase(
        std::unique(hit_targets.begin(), hit_targets.end(), [](Entity a, Entity b) { return a.entity_id == b.entity_id; }),
        hit_targets.end());
  }

  for (Entity t : hit_targets) {
    OnHit(world, &sim, self, t);
    if (emit_damage) {
      Damage out = dmg;
      out.amount = compute_damage_amount();
      sim.emit_effect(make_damage_effect(src, t, out));
    }
  }

  if (consume_on_hit) {
    mark_destroyed(world, self);
  }
}

} // namespace arksim
