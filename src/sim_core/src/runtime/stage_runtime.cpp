#include "sim_core/runtime/stage_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "sim_core/components/area.hpp"
#include "sim_core/components/attack.hpp"
#include "sim_core/components/attack_power.hpp"
#include "sim_core/components/damage.hpp"
#include "sim_core/components/destroyed.hpp"
#include "sim_core/components/hp.hpp"
#include "sim_core/components/position.hpp"
#include "sim_core/components/route_move.hpp"
#include "sim_core/components/spatial.hpp"
#include "sim_core/core/rng.hpp"
#include "sim_core/effects/effects.hpp"
#include "sim_core/nav/arknights_enemy_database.hpp"
#include "sim_core/nav/arknights_level.hpp"
#include "sim_core/runtime/sim_context.hpp"
#include "sim_core/runtime/sim_state.hpp"
#include "sim_core/runtime/world_runtime.hpp"

namespace arksim {
namespace {

constexpr std::uint64_t kStageLeakTriggerName = 0x53544147455F4C45ull; // "STAGE_LE"
constexpr std::uint64_t kEnemyAttackOnFireName = 0x454E455F41544B00ull; // "ENE_ATK\0"

constexpr Tick kTicksPerSecond = Tick{1} << 30;

constexpr f32 kDefaultEnemyMeleeRangeRadius = 0.8f;
constexpr f32 kDefaultEnemyRangedRangeRadius = 2.0f;

Tick ticks_from_seconds_ceil(double seconds) {
  const long double scaled =
      std::ceil(static_cast<long double>(seconds) * static_cast<long double>(kTicksPerSecond));
  if (scaled <= 0.0L) {
    return 0;
  }
  const long double maxv = static_cast<long double>(std::numeric_limits<Tick>::max());
  if (scaled >= maxv) {
    return std::numeric_limits<Tick>::max();
  }
  return static_cast<Tick>(scaled);
}

void on_stage_leak(World& world, SimState* sim, Entity leaked) {
  (void)leaked;
  if (!sim) {
    return;
  }

  auto* stage = world.try_get<StageRuntime>(sim->world_entity());
  if (!stage || !stage->active) {
    return;
  }

  stage->leaked += 1;
  stage->life_point = std::max(0, stage->life_point - 1);
}

} // namespace

namespace {

void start_arknights_stage_internal(SimState& sim,
                                   SimContext& ctx,
                                   const ArknightsLevel& level,
                                   const ArknightsEnemyDatabase* enemies) {
  ctx.map = level.map;
  ctx.spatial.reset(ctx.map.width(), ctx.map.height());
  ctx.path_cache.clear();
  ctx.entities.clear();
  ctx.target_scratch.candidates.clear();
  ctx.target_scratch.scored.clear();
  ctx.target_scratch.targets.clear();
  ctx.projectile_scratch.candidates.clear();
  ctx.projectile_scratch.hit_targets.clear();
  ctx.arknights_level = &level;
  ctx.arknights_enemy_db = enemies;

  World& world = sim.world();
  auto& stage = world.add<StageRuntime>(sim.world_entity());
  stage.start(world, sim, ctx, level);
}

} // namespace

void StageRuntime::start(World& world, SimState& sim, SimContext& ctx, const ArknightsLevel& level) {
  (void)world;
  active = true;
  start_tick = sim.tick();
  next_spawn = 0;

  max_life_point = level.max_life_point;
  life_point = level.max_life_point;
  leaked = 0;

  ctx.arknights_level = &level;
  sim.world_runtime().OnLeak.add(kStageLeakTriggerName, /*priority=*/0, &on_stage_leak);
}

void StageRuntime::step(World& world, SimState& sim, SimContext& ctx, const ArknightsLevel& level) {
  if (!active) {
    return;
  }
  if (ctx.arknights_level != &level) {
    // StageRuntime expects SimContext to point to the same stage data.
    // Avoid dereferencing stale pointers.
    return;
  }

  Tick stage_tick = 0;
  if (sim.tick() >= start_tick) {
    stage_tick = sim.tick() - start_tick;
  }

  Rng& rng = sim.rng();

  auto sample_extent = [&](f32 extent) -> f32 {
    const f32 r = std::abs(extent);
    if (!(r > 0.0f)) {
      return 0.0f;
    }
    return rng.uniform_f32(-r, r);
  };

  while (next_spawn < level.spawns.size()) {
    const ArknightsSpawnEvent& ev = level.spawns[next_spawn];
    if (ev.spawn_tick > stage_tick) {
      break;
    }
    ++next_spawn;

    if (ev.route_index >= static_cast<std::uint32_t>(level.routes.size())) {
      continue;
    }
    const ArknightsRoute& route = level.routes[ev.route_index];
    if (!route.valid) {
      continue;
    }

    Entity e = world.create();

    // Spawn position = start tile center + spawnOffset + uniform random range.
    const vec<f32> start = Map::tile_center(route.start_tile);
    const vec<f32> jitter{sample_extent(route.spawn_random_range.x), sample_extent(route.spawn_random_range.y)};
    const vec<f32> cursor_pos = start + route.spawn_offset + jitter;

    Position pos;
    pos.pos = cursor_pos; // entityPos == cursorPos for now (cursor_offset is 0)
    world.add<Position>(e, pos);

    const ArknightsEnemyDatabase* enemies = ctx.arknights_enemy_db;
    const ArknightsEnemyRef* enemy_ref = level.find_enemy_db_ref(ev.key);

    bool has_enemy_stats = false;
    ArknightsEnemyStats enemy_stats{};
    if (enemies) {
      int enemy_level = 0;
      if (enemy_ref) {
        enemy_level = enemy_ref->level;
      }

      const ArknightsEnemyStats* base = enemies->find(ev.key, enemy_level);
      if (!base && enemy_level != 0) {
        base = enemies->find(ev.key, 0);
      }

      if (base) {
        enemy_stats = *base;
        has_enemy_stats = true;

        if (enemy_ref) {
          auto apply_opt = [&](const std::optional<double>& ov, double& field) {
            if (ov.has_value()) {
              field = *ov;
            }
          };

          apply_opt(enemy_ref->overridden.max_hp, enemy_stats.max_hp);
          apply_opt(enemy_ref->overridden.atk, enemy_stats.atk);
          apply_opt(enemy_ref->overridden.def, enemy_stats.def);
          apply_opt(enemy_ref->overridden.magic_res, enemy_stats.magic_res);
          apply_opt(enemy_ref->overridden.move_speed, enemy_stats.move_speed);
          apply_opt(enemy_ref->overridden.attack_speed, enemy_stats.attack_speed);
          apply_opt(enemy_ref->overridden.base_attack_time, enemy_stats.base_attack_time);
        }
      }
    }

    RouteMove rm = route.instantiate(rng);
    rm.game_time = stage_tick;
    rm.wave_time = stage_tick - ev.wave_start_tick;
    rm.fragment_time = stage_tick - ev.fragment_start_tick;
    if (has_enemy_stats && enemy_stats.move_speed > 0.0) {
      rm.move_speed = BuffNum(enemy_stats.move_speed);
    }
    world.add<RouteMove>(e, rm);

    Spatial sp;
    sp.flags = TypeFlags::Enemy | TypeFlags::Normal;
    if (route.mode == MoveMode::Air) {
      sp.flags |= TypeFlags::Air;
    } else {
      sp.flags |= TypeFlags::Ground | TypeFlags::Blockable;
    }
    if (has_enemy_stats) {
      switch (enemy_stats.apply_way) {
        case ArknightsEnemyStats::ApplyWay::Melee:
          sp.flags |= TypeFlags::Melee;
          break;
        case ArknightsEnemyStats::ApplyWay::Ranged:
          sp.flags |= TypeFlags::Ranged;
          break;
        case ArknightsEnemyStats::ApplyWay::Unknown:
          break;
      }
    }
    world.add<Spatial>(e, sp);

    Area area;
    area.type = Area::Type::Circle;
    area.radius = vec<f32>{0.35f, 0.0f};
    world.add<Area>(e, area);

    if (has_enemy_stats) {
      if (enemy_stats.max_hp > 0.0) {
        HP hp;
        hp.total_hp = BuffNum(enemy_stats.max_hp);
        hp.ratio = 1.0;
        world.add<HP>(e, hp);
      }

      auto& stats = ensure_defstats(world, e);
      stats.def = BuffNum(enemy_stats.def);
      stats.res = BuffNum(enemy_stats.magic_res);

      AttackPower ap;
      ap.atk = BuffNum(enemy_stats.atk);
      world.add<AttackPower>(e, ap);

      // Minimal enemy melee attack:
      // - Targets allies within a small circle
      // - Emits a Damage effect based on AttackPower
      if (enemy_stats.atk > 0.0 && enemy_stats.base_attack_time > 0.0) {
        Attack atk;
        atk.scan_interval = 0; // scan every step for now
        atk.scan_remain = 0;
        atk.range_kind = TargetRange::Kind::Circle;
        atk.range_grid = TargetRange::Grid::Occupation;
        atk.required = TypeFlags::Ally;
        const f32 default_radius = (enemy_stats.apply_way == ArknightsEnemyStats::ApplyWay::Ranged)
                                      ? kDefaultEnemyRangedRangeRadius
                                      : kDefaultEnemyMeleeRangeRadius;
        const f32 radius = (enemy_stats.has_range_radius && enemy_stats.range_radius > 0.0)
                               ? static_cast<f32>(enemy_stats.range_radius)
                               : default_radius;
        atk.range_radius = radius;
        atk.occupation_circle_intersect = true;

        // Per docs/targetSelector.wiki: enemies generally prefer the last deployed target (CreatedTimeDesc).
        atk.arranger.primary = TargetArranger::Primary::CreatedTimeDesc;
        atk.arranger.relationship = TargetArranger::Relationship::Hostile;
        atk.arranger.max_targets = 1;

        // enemy_stats.attack_speed: 100 == normal.
        atk.atk_speed = static_cast<float>(enemy_stats.attack_speed - 100.0);
        atk.base_interval = ticks_from_seconds_ceil(enemy_stats.base_attack_time);
        atk.base_pre = 0;
        atk.base_post = 0;

        const Damage::TypeMask kind_flag =
            (enemy_stats.apply_way == ArknightsEnemyStats::ApplyWay::Ranged) ? Damage::ranged : Damage::melee;
        atk.OnFire.add(kEnemyAttackOnFireName, /*priority=*/0, [kind_flag](World& world,
                                                                          SimState* sim,
                                                                          Entity self,
                                                                          std::span<const Entity> targets,
                                                                          Attack&) {
          if (!sim) {
            return;
          }
          if (targets.empty()) {
            return;
          }

          // Per docs/targetSelector.wiki: when blocked, enemies prioritize attacking their blocker.
          Entity target = targets[0];
          if (const auto* rm = world.try_get<RouteMove>(self)) {
            const Entity blocker = rm->blocked_by;
            if (blocker.entity_id != 0 && world.is_alive(blocker) && !world.has<Destroyed>(blocker)) {
              target = blocker;
            }
          }
          const auto* ap = world.try_get<AttackPower>(self);
          if (!ap) {
            return;
          }
          const double amount = ap->atk.value();
          if (!(amount > 0.0)) {
            return;
          }
          const Damage dmg{amount, Damage::physical | kind_flag};
          sim->emit_effect(make_damage_effect(self, target, dmg));
        });

        world.add<Attack>(e, atk);
      }
    }
  }
}

void start_arknights_stage(SimState& sim, SimContext& ctx, const ArknightsLevel& level) {
  start_arknights_stage_internal(sim, ctx, level, nullptr);
}

void start_arknights_stage(SimState& sim,
                           SimContext& ctx,
                           const ArknightsLevel& level,
                           const ArknightsEnemyDatabase& enemies) {
  start_arknights_stage_internal(sim, ctx, level, &enemies);
}

} // namespace arksim
