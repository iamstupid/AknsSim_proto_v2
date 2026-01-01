#include "sim_core/runtime/stage_runtime.hpp"

#include <algorithm>
#include <cmath>

#include "sim_core/components/area.hpp"
#include "sim_core/components/position.hpp"
#include "sim_core/components/route_move.hpp"
#include "sim_core/components/spatial.hpp"
#include "sim_core/core/rng.hpp"
#include "sim_core/nav/arknights_level.hpp"
#include "sim_core/runtime/sim_context.hpp"
#include "sim_core/runtime/sim_state.hpp"
#include "sim_core/runtime/world_runtime.hpp"

namespace arksim {
namespace {

constexpr std::uint64_t kStageLeakTriggerName = 0x53544147455F4C45ull; // "STAGE_LE"

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

    RouteMove rm = route.instantiate(rng);
    rm.game_time = stage_tick;
    rm.wave_time = stage_tick - ev.wave_start_tick;
    rm.fragment_time = stage_tick - ev.fragment_start_tick;
    world.add<RouteMove>(e, rm);

    Spatial sp;
    sp.flags = TypeFlags::Enemy | TypeFlags::Normal;
    if (route.mode == MoveMode::Air) {
      sp.flags |= TypeFlags::Air;
    } else {
      sp.flags |= TypeFlags::Ground | TypeFlags::Blockable;
    }
    world.add<Spatial>(e, sp);

    Area area;
    area.type = Area::Type::Circle;
    area.radius = vec<f32>{0.35f, 0.0f};
    world.add<Area>(e, area);
  }
}

void start_arknights_stage(SimState& sim, SimContext& ctx, const ArknightsLevel& level) {
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

  World& world = sim.world();
  auto& stage = world.add<StageRuntime>(sim.world_entity());
  stage.start(world, sim, ctx, level);
}

} // namespace arksim

