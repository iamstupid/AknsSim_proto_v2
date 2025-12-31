#include "sim_core/runtime/sim_state.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
#include <intrin.h>
#endif

#include "sim_core/scripting/script_vm.hpp"
#include "sim_core/ecs/destroyed.hpp"
#include "sim_core/effects/effects.hpp"
#include "sim_core/runtime/sim_context.hpp"
#include "sim_core/runtime/world_runtime.hpp"
#include "sim_core/components/attack.hpp"
#include "sim_core/components/block.hpp"
#include "sim_core/components/barrier_shield.hpp"
#include "sim_core/components/buff.hpp"
#include "sim_core/components/destroyed.hpp"
#include "sim_core/components/position.hpp"
#include "sim_core/components/projectile.hpp"
#include "sim_core/components/route_move.hpp"
#include "sim_core/components/spatial.hpp"
#include "sim_core/components/unbalance.hpp"

namespace arksim {

SimState::SimState(std::uint64_t seed) : rng_(seed) {
  register_core_effect_handlers(effect_handler_);
  world_entity_ = world_.create();
  world_.add<WorldRuntime>(world_entity_);
}

void SimState::set_tick_rate(Tick tick_rate) {
  tick_rate_ = tick_rate;
}

WorldRuntime& SimState::world_runtime() {
  return world_.get<WorldRuntime>(world_entity_);
}

const WorldRuntime& SimState::world_runtime() const {
  const auto* runtime = world_.try_get<WorldRuntime>(world_entity_);
  // Must exist: SimState creates and snapshots a WorldRuntime singleton entity.
  assert(runtime != nullptr);
  return *runtime;
}

namespace {

std::uint64_t ceil_mul_div_u64(std::uint64_t a, std::uint64_t b, std::uint64_t c) {
  if (c == 0) {
    return 0;
  }

#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
  unsigned __int64 high = 0;
  unsigned __int64 low = _umul128(a, b, &high);
  unsigned __int64 rem = 0;
  unsigned __int64 quo = _udiv128(high, low, c, &rem);
  if (rem != 0 && quo != std::numeric_limits<std::uint64_t>::max()) {
    ++quo;
  }
  return quo;
#elif defined(__SIZEOF_INT128__)
  unsigned __int128 prod = static_cast<unsigned __int128>(a) * b;
  unsigned __int128 quo = prod / c;
  unsigned __int128 rem = prod % c;
  if (rem != 0) {
    ++quo;
  }
  if (quo > std::numeric_limits<std::uint64_t>::max()) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return static_cast<std::uint64_t>(quo);
#else
  const double scaled = static_cast<double>(a) * static_cast<double>(b) / static_cast<double>(c);
  if (scaled >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return static_cast<std::uint64_t>(scaled == std::floor(scaled) ? scaled : std::floor(scaled) + 1.0);
#endif
}

} // namespace

void SimState::set_tick_rate(std::uint64_t numerator, std::uint64_t denominator) {
  tick_rate_ = ceil_mul_div_u64(numerator, kTicksPerSecond, denominator);
}

float SimState::get_tick_rate_f32() const {
  return static_cast<float>(tick_rate_) / static_cast<float>(kTicksPerSecond);
}

double SimState::get_tick_rate_f64() const {
  return static_cast<double>(tick_rate_) / static_cast<double>(kTicksPerSecond);
}

void SimState::attach_script(ScriptVM* script) {
  script_ = script;
  if (script_) {
    script_->bind_world(this);
  }
}

void SimState::step() {
  cleanup_destroyed(world_);
  resolve();
  tick_ += tick_rate_;
}

namespace {

void sort_by_entity_id(std::vector<Entity>& v) {
  std::sort(v.begin(), v.end(), [](Entity a, Entity b) { return a.entity_id < b.entity_id; });
}

} // namespace

std::size_t SimState::step_frame(SimContext& ctx, std::size_t max_effects) {
  cleanup_destroyed(world_, ctx.entities);
  resolve();

  // 0) Block system (uses spatial from previous frame; clears orphaned blocks first).
  ctx.entities.clear();
  world_.query<RouteMove, Spatial>([&](Entity e, RouteMove&, Spatial&) { ctx.entities.push_back(e); });
  sort_by_entity_id(ctx.entities);
  for (Entity e : ctx.entities) {
    if (!world_.is_alive(e) || world_.has<Destroyed>(e)) {
      continue;
    }
    auto* rm = world_.try_get<RouteMove>(e);
    auto* sp = world_.try_get<Spatial>(e);
    if (!rm || !sp) {
      continue;
    }
    if (!rm->is_blocked()) {
      continue;
    }

    if (!world_.is_alive(rm->blocked_by) || !world_.has<Blocker>(rm->blocked_by)) {
      rm->clear_block();
      sp->flags &= ~TypeFlags::Blocked;
    }
  }

  ctx.entities.clear();
  world_.query<Blocker, Position>([&](Entity e, Blocker&, Position&) { ctx.entities.push_back(e); });
  sort_by_entity_id(ctx.entities);
  for (Entity e : ctx.entities) {
    if (!world_.is_alive(e) || world_.has<Destroyed>(e)) {
      continue;
    }
    auto* blk = world_.try_get<Blocker>(e);
    if (!blk) {
      continue;
    }
    blk->step(world_, e, ctx.spatial, tick_rate_);
  }

  // 1) Movement systems (must run before spatial rebuild).
  ctx.entities.clear();
  world_.query<RouteMove, Position>([&](Entity e, RouteMove&, Position&) { ctx.entities.push_back(e); });
  sort_by_entity_id(ctx.entities);
  for (Entity e : ctx.entities) {
    if (!world_.is_alive(e) || world_.has<Destroyed>(e)) {
      continue;
    }
    auto* rm = world_.try_get<RouteMove>(e);
    auto* pos = world_.try_get<Position>(e);
    if (!rm || !pos) {
      continue;
    }
    rm->step(world_, e, *pos, ctx.map, ctx.path_cache, tick_rate_);
  }

  ctx.entities.clear();
  world_.query<Unbalance, Position>([&](Entity e, Unbalance&, Position&) { ctx.entities.push_back(e); });
  sort_by_entity_id(ctx.entities);
  for (Entity e : ctx.entities) {
    if (!world_.is_alive(e) || world_.has<Destroyed>(e)) {
      continue;
    }
    auto* ub = world_.try_get<Unbalance>(e);
    auto* pos = world_.try_get<Position>(e);
    if (!ub || !pos) {
      continue;
    }
    ub->step(world_, e, *pos, ctx.map, tick_rate_);
  }

  // 1.5) Hole kill (after movement, before spatial rebuild):
  // Any non-flying unit stepping on a Hole tile is marked Destroyed.
  ctx.entities.clear();
  world_.query<RouteMove, Position>([&](Entity e, RouteMove&, Position&) { ctx.entities.push_back(e); });
  world_.query<Unbalance, Position>([&](Entity e, Unbalance&, Position&) { ctx.entities.push_back(e); });
  sort_by_entity_id(ctx.entities);
  ctx.entities.erase(std::unique(ctx.entities.begin(),
                                 ctx.entities.end(),
                                 [](Entity a, Entity b) { return a.entity_id == b.entity_id; }),
                     ctx.entities.end());

  for (Entity e : ctx.entities) {
    if (!world_.is_alive(e) || world_.has<Destroyed>(e)) {
      continue;
    }

    const auto* pos = world_.try_get<Position>(e);
    if (!pos) {
      continue;
    }

    // Determine whether the unit is flying (immune to holes).
    bool is_flying = false;
    if (const auto* rm = world_.try_get<RouteMove>(e)) {
      is_flying = (rm->mode == MoveMode::Air);
    } else if (const auto* ub = world_.try_get<Unbalance>(e)) {
      is_flying = (ub->mode == MoveMode::Air);
    }
    if (const auto* sp = world_.try_get<Spatial>(e)) {
      if (has_flags(sp->flags, TypeFlags::Air | TypeFlags::Flight)) {
        is_flying = true;
      }
    }
    if (is_flying) {
      continue;
    }

    vec<f32> cursor_pos = pos->pos;
    if (const auto* rm = world_.try_get<RouteMove>(e)) {
      cursor_pos = pos->pos + rm->cursor_offset;
    }

    const TileCoord t = Map::tile_at(cursor_pos);
    if (!ctx.map.in_bounds(t)) {
      continue;
    }
    if (ctx.map.is_hole(t)) {
      mark_destroyed(world_, e);
    }
  }

  // 1.6) Reach end (after movement, before spatial rebuild):
  // Reaching the end point is not tied to RouteMove::step (e.g. if RouteMove is inactive).
  ctx.entities.clear();
  world_.query<RouteMove, Position>([&](Entity e, RouteMove&, Position&) { ctx.entities.push_back(e); });
  sort_by_entity_id(ctx.entities);

  for (Entity e : ctx.entities) {
    if (!world_.is_alive(e) || world_.has<Destroyed>(e)) {
      continue;
    }

    auto* rm = world_.try_get<RouteMove>(e);
    const auto* pos = world_.try_get<Position>(e);
    if (!rm || !pos) {
      continue;
    }
    if (!rm->has_end || rm->reached_end) {
      continue;
    }
    if (rm->visit_every_checkpoint && !rm->all_checkpoints_completed()) {
      continue;
    }

    const vec<f32> cursor_pos = pos->pos + rm->cursor_offset;
    const vec<f32> d = cursor_pos - rm->end_point;
    if (d.length_sq() <= 0.05f * 0.05f) {
      rm->reached_end = true;
      emit_effect(make_leak_effect(e));
      mark_destroyed(world_, e);
    }
  }

  // 2) Spatial rebuild (Position/Area/Spatial).
  ctx.spatial.rebuild(world_);

  // 3) Combat systems (may emit effects).
  ctx.entities.clear();
  world_.each<Attack>([&](Entity e, Attack&) { ctx.entities.push_back(e); });
  sort_by_entity_id(ctx.entities);
  for (Entity e : ctx.entities) {
    auto* atk = world_.try_get<Attack>(e);
    if (!atk) {
      continue;
    }
    atk->step(world_, *this, e, ctx.spatial, tick_rate_, ctx.target_scratch);
  }

  ctx.entities.clear();
  world_.each<Projectile>([&](Entity e, Projectile&) { ctx.entities.push_back(e); });
  sort_by_entity_id(ctx.entities);
  for (Entity e : ctx.entities) {
    auto* proj = world_.try_get<Projectile>(e);
    if (!proj) {
      continue;
    }
    proj->step(world_, *this, e, ctx.spatial, tick_rate_, ctx.projectile_scratch);
  }

  // 4) Buff/Barrier/Shield upkeep.
  ctx.entities.clear();
  world_.each<Barrier>([&](Entity e, Barrier&) { ctx.entities.push_back(e); });
  sort_by_entity_id(ctx.entities);
  for (Entity e : ctx.entities) {
    if (!world_.is_alive(e) || world_.has<Destroyed>(e)) {
      continue;
    }
    if (auto* barrier = world_.try_get<Barrier>(e)) {
      barrier->step(world_);
    }
  }

  ctx.entities.clear();
  world_.each<Shield>([&](Entity e, Shield&) { ctx.entities.push_back(e); });
  sort_by_entity_id(ctx.entities);
  for (Entity e : ctx.entities) {
    if (!world_.is_alive(e) || world_.has<Destroyed>(e)) {
      continue;
    }
    if (auto* shield = world_.try_get<Shield>(e)) {
      shield->step(world_, tick_rate_);
    }
  }

  ctx.entities.clear();
  world_.each<Buff>([&](Entity e, Buff&) { ctx.entities.push_back(e); });
  sort_by_entity_id(ctx.entities);
  for (Entity e : ctx.entities) {
    if (!world_.is_alive(e) || world_.has<Destroyed>(e)) {
      continue;
    }
    if (auto* buff = world_.try_get<Buff>(e)) {
      buff->step(world_, e, tick_rate_);
    }
  }

  // 5) Resolve effects deterministically at end-of-frame.
  const std::size_t processed = process_all_effects(max_effects);

  tick_ += tick_rate_;
  return processed;
}

void SimState::emit_effect(Effect effect) {
  queue_.push(effect);
}

bool SimState::process_one_effect() {
  Effect effect;
  if (!queue_.try_pop(effect)) {
    return false;
  }
  effect_handler_(effect);
  return true;
}

std::size_t SimState::process_all_effects(std::size_t max_effects) {
  std::size_t processed = 0;
  while (processed < max_effects) {
    if (!process_one_effect()) {
      break;
    }
    ++processed;
  }
  return processed;
}

void SimState::resolve() {
  if (script_) {
    script_->call_on_tick(tick_);
  }
}

std::uint64_t SimState::state_hash() const {
  std::uint64_t hash = 1469598103934665603ULL;
  auto mix = [&hash](std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };

  mix(static_cast<std::uint64_t>(tick_));
  mix(static_cast<std::uint64_t>(tick_rate_));
  mix(rng_.state());
  mix(static_cast<std::uint64_t>(queue_.size()));

  return hash;
}

SimState::Snapshot SimState::snapshot() const {
  Snapshot snap;
  snap.tick = tick_;
  snap.tick_rate = tick_rate_;
  snap.rng = rng_.snapshot();
  snap.world = world_.snapshot();
  snap.queue = queue_.snapshot();
  snap.handlers = effect_handler_.handlers;
  snap.world_entity = world_entity_;
  return snap;
}

void SimState::restore(const Snapshot& snap) {
  // NOTE: Script state is intentionally excluded from snapshots. Scripts must be stateless / rebuildable.
  tick_ = snap.tick;
  tick_rate_ = snap.tick_rate;
  rng_.restore(snap.rng);
  world_.restore(snap.world);
  world_entity_ = snap.world_entity;
  queue_.restore(snap.queue);
  effect_handler_.handlers = snap.handlers;
  effect_handler_.bind(this);

  if (script_) {
    script_->bind_world(this);
  }
}

} // namespace arksim
