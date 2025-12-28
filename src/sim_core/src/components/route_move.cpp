#include "sim_core/components/route_move.hpp"

#include <algorithm>
#include <cmath>

#include "sim_core/components/destroyed.hpp"
#include "sim_core/components/position.hpp"
#include "sim_core/movement_utils.hpp"
#include "sim_core/components/unbalance.hpp"
#include "sim_core/path_map.hpp"

namespace arksim {

namespace {

constexpr Tick kAvoidInterval = (kTicksPerSecond + 9) / 10; // ceil(0.1s)

inline f32 theoretical_speed(const RouteMove& rm) {
  const f32 base = std::max(static_cast<f32>(rm.move_speed.value()), 0.1f);
  return base * rm.move_multiplier;
}

struct SteeringResult {
  vec<f32> vel{};
  vec<f32> disp{};
};

SteeringResult steering_and_integrate(const RouteMove& rm,
                                      const vec<f32>& inertia_vel,
                                      const vec<f32>& given_dir,
                                      const vec<f32>& avoid_force,
                                      f32 dt) {
  const f32 theo = theoretical_speed(rm);
  if (!(theo > 0.0f) || !(dt > 0.0f)) {
    return SteeringResult{};
  }

  const f32 inertia_mag = inertia_vel.length();
  const f32 scale = std::max(inertia_mag / std::max(theo, 1e-6f), 0.5f);
  const vec<f32> actual_avoid = avoid_force * scale;

  const vec<f32> desired_vel = given_dir * theo;
  vec<f32> accel = (desired_vel - inertia_vel) * rm.steering_factor + actual_avoid;
  accel = clamp_magnitude(accel, rm.max_steering_force);

  vec<f32> vel = inertia_vel + accel * dt;
  vel = clamp_magnitude(vel, theo);

  return SteeringResult{vel, vel * dt};
}

inline bool is_move_cp(RouteMove::CheckPoint::Type type) {
  using Type = RouteMove::CheckPoint::Type;
  switch (type) {
    case Type::Move:
    case Type::PatrolMove:
      return true;
    default:
      return false;
  }
}

inline bool is_move_or_teleport_cp(RouteMove::CheckPoint::Type type) {
  using Type = RouteMove::CheckPoint::Type;
  switch (type) {
    case Type::Move:
    case Type::PatrolMove:
    case Type::Disappear:
    case Type::AppearAtPos:
      return true;
    default:
      return false;
  }
}

Tick ticks_from_seconds_ceil(double seconds) {
  const long double scaled = std::ceil(static_cast<long double>(seconds) * static_cast<long double>(kTicksPerSecond));
  if (scaled <= 0.0L) {
    return 0;
  }
  return static_cast<Tick>(scaled);
}

void on_enter_checkpoint(RouteMove& rm, RouteMove::CheckPoint& cp, Position& pos) {
  using Type = RouteMove::CheckPoint::Type;
  switch (cp.type) {
    case Type::WaitForSeconds:
      rm.waited_ticks = 0;
      break;
    case Type::WaitBossrushWave:
      cp.start_region = rm.region_index;
      break;
    case Type::Disappear:
      rm.runtime_state = RouteMove::RuntimeState::Disappeared;
      break;
    case Type::AppearAtPos:
      rm.runtime_state = RouteMove::RuntimeState::Move;
      pos.pos = cp.appear_cursor_pos - rm.cursor_offset;
      set_position_velocity(pos, vec<f32>{});
      break;
    case Type::Move:
    case Type::PatrolMove:
    case Type::Alert:
    case Type::WaitForPlayTime:
    case Type::WaitCurrentFragmentTime:
    case Type::WaitCurrentWaveTime:
    default:
      break;
  }
}

void update_current_checkpoint(RouteMove& rm, const RouteMove::CheckPoint& cp, Tick tick_rate) {
  using Type = RouteMove::CheckPoint::Type;
  switch (cp.type) {
    case Type::WaitForSeconds:
      rm.waited_ticks += tick_rate;
      break;
    default:
      break;
  }
}

bool is_checkpoint_complete(const RouteMove& rm,
                            const RouteMove::CheckPoint& cp,
                            const Position& pos,
                            const Map& map,
                            PathMapCache& cache) {
  using Type = RouteMove::CheckPoint::Type;
  const vec<f32> cursor_pos = pos.pos + rm.cursor_offset;

  switch (cp.type) {
    case Type::Move:
    case Type::PatrolMove: {
      const f32 r = std::max(cp.radius, 0.05f);
      if (distance(cursor_pos, cp.target_point) <= r) {
        return true;
      }

      if (rm.mode == MoveMode::Air) {
        return false;
      }

      const TileCoord t = Map::tile_at(cursor_pos);
      if (!map.passable(t, rm.mode)) {
        return true;
      }

      const PathMap& pm = cache.get_ground(cp.target_tile, rm.allow_diagonal_move);
      if (!pm.in_bounds(t) || pm.dist(t) == PathMap::kInf) {
        return true;
      }

      return false;
    }
    case Type::WaitForSeconds:
      return rm.waited_ticks >= cp.wait_ticks;
    case Type::WaitForPlayTime:
      return rm.game_time >= cp.play_time_ticks;
    case Type::WaitCurrentFragmentTime:
      return rm.fragment_time >= cp.fragment_time_ticks;
    case Type::WaitCurrentWaveTime:
      return rm.wave_time >= cp.wave_time_ticks;
    case Type::Disappear:
    case Type::AppearAtPos:
    case Type::Alert:
      return true;
    case Type::WaitBossrushWave:
      return rm.region_index >= (cp.start_region + cp.wait_regions);
    default:
      return false;
  }
}

std::uint32_t next_checkpoint_index_with_patrol_rule(const RouteMove& rm,
                                                     std::uint32_t current_index,
                                                     const RouteMove::CheckPoint& cp) {
  const std::uint32_t next = current_index + 1;
  if (cp.type == RouteMove::CheckPoint::Type::PatrolMove && !rm.cps.empty() &&
      current_index == static_cast<std::uint32_t>(rm.cps.size() - 1) && current_index != 0) {
    return 0;
  }
  return next;
}

void advance_checkpoints(RouteMove& rm, Position& pos, const Map& map, PathMapCache& cache) {
  const std::size_t max_iters = rm.cps.size() + 2;
  for (std::size_t iter = 0; iter < max_iters; ++iter) {
    auto* cp = rm.current_cp();
    if (!cp) {
      return;
    }

    if (rm.ignore_all_but_move_cp && !is_move_or_teleport_cp(cp->type)) {
      ++rm.cp_index;
      continue;
    }

    if (rm.entered_cp_index != rm.cp_index) {
      on_enter_checkpoint(rm, *cp, pos);
      rm.entered_cp_index = rm.cp_index;
    }

    if (!is_checkpoint_complete(rm, *cp, pos, map, cache)) {
      return;
    }

    rm.cp_index = next_checkpoint_index_with_patrol_rule(rm, rm.cp_index, *cp);
  }
}

void step_move_only(RouteMove& rm,
                    World& world,
                    Entity self,
                    Position& pos,
                    const Map& map,
                    PathMapCache& cache,
                    Tick tick_rate,
                    TileCoord target_tile,
                    const vec<f32>& target_point) {
  const f32 dt = tick_rate_to_seconds_f32(tick_rate);
  if (!(dt > 0.0f)) {
    return;
  }

  // Step 0: out-of-bounds correction (entity pos).
  if (!map.in_world_bounds(pos.pos)) {
    const vec<f32> inside = map.clamp_to_bounds(pos.pos);
    const vec<f32> dir_in = normalize_or_zero(inside - pos.pos);
    const vec<f32> disp = dir_in * (theoretical_speed(rm) * dt);
    pos.pos += disp;
    set_position_velocity(pos, dir_in * theoretical_speed(rm));
    return;
  }

  const vec<f32> cursor_pos = pos.pos + rm.cursor_offset;

  // Step 1/2: determine target and given direction.
  vec<f32> target_pos = target_point;
  vec<f32> given_dir{};

  if (rm.mode == MoveMode::Air) {
    given_dir = normalize_or_zero(target_point - cursor_pos);
  } else {
    const PathMap& pm = cache.get_ground(target_tile, rm.allow_diagonal_move);
    const TileCoord cur_tile = Map::tile_at(cursor_pos);
    if (!pm.in_bounds(cur_tile) || !map.passable(cur_tile, MoveMode::Ground) || pm.dist(cur_tile) == PathMap::kInf) {
      given_dir = vec<f32>{};
    } else {
      const TileCoord next = rm.allow_diagonal_move ? pm.next_smooth(cur_tile) : pm.next_raw(cur_tile);
      target_pos = (next == target_tile) ? target_point : Map::tile_center(next);
      given_dir = normalize_or_zero(target_pos - cursor_pos);
    }
  }

  // Step 1.5: reach target in a single step (rare).
  const f32 max_move = theoretical_speed(rm) * dt;
  if (distance(cursor_pos, target_pos) <= max_move) {
    pos.pos = target_pos - rm.cursor_offset;
    pos.current_speed = 0.0f;
    return;
  }

  // Step 2.5: bound => no displacement and no inertia update.
  if (rm.is_bound) {
    return;
  }

  // Step 3: avoidance (every ~0.1s).
  if (rm.avoid_remain == 0 || rm.avoid_remain <= tick_rate) {
    rm.cached_avoid = compute_avoidance_force(map, rm.mode, pos.pos, cursor_pos, rm.foot_offset, /*half_body_width=*/0.2f, given_dir);
    rm.avoid_remain = kAvoidInterval;
  } else {
    rm.avoid_remain -= tick_rate;
  }

  // Step 4: steering/inertia integration.
  const vec<f32> inertia_vel = position_velocity(pos);
  const SteeringResult res = steering_and_integrate(rm, inertia_vel, given_dir, rm.cached_avoid, dt);

  // Step 5: apply with impassable correction (reflect).
  const vec<f32> out_vel = apply_displacement_with_correction(map, rm.mode, pos, res.disp, dt);
  set_position_velocity(pos, out_vel);
}

} // namespace

const RouteMove::CheckPoint* RouteMove::current_cp() const {
  if (cp_index >= cps.size()) {
    return nullptr;
  }
  return &cps[cp_index];
}

RouteMove::CheckPoint* RouteMove::current_cp() {
  if (cp_index >= cps.size()) {
    return nullptr;
  }
  return &cps[cp_index];
}

void RouteMove::clear_route() {
  runtime_state = RuntimeState::Move;
  visit_every_checkpoint = false;
  ignore_all_but_move_cp = false;

  cps.clear();
  cp_index = 0;
  entered_cp_index = static_cast<std::uint32_t>(-1);

  game_time = 0;
  fragment_time = 0;
  wave_time = 0;
  waited_ticks = 0;
  region_index = 0;

  has_end = false;
  end_point = vec<f32>{};
  end_tile = TileCoord{};
  reached_end = false;
}

void RouteMove::push_move_cp(TileCoord tile, vec<f32> point, f32 radius) {
  CheckPoint cp;
  cp.type = CheckPoint::Type::Move;
  cp.target_tile = tile;
  cp.target_point = point;
  cp.radius = radius;
  cps.push_back(cp);
}

void RouteMove::push_patrol_move_cp(TileCoord tile, vec<f32> point, f32 radius) {
  CheckPoint cp;
  cp.type = CheckPoint::Type::PatrolMove;
  cp.target_tile = tile;
  cp.target_point = point;
  cp.radius = radius;
  cps.push_back(cp);
}

void RouteMove::push_wait_seconds(double seconds) {
  CheckPoint cp;
  cp.type = CheckPoint::Type::WaitForSeconds;
  cp.wait_ticks = ticks_from_seconds_ceil(seconds);
  cps.push_back(cp);
}

void RouteMove::push_wait_play_time(double seconds) {
  CheckPoint cp;
  cp.type = CheckPoint::Type::WaitForPlayTime;
  cp.play_time_ticks = ticks_from_seconds_ceil(seconds);
  cps.push_back(cp);
}

void RouteMove::push_wait_fragment_time(double seconds) {
  CheckPoint cp;
  cp.type = CheckPoint::Type::WaitCurrentFragmentTime;
  cp.fragment_time_ticks = ticks_from_seconds_ceil(seconds);
  cps.push_back(cp);
}

void RouteMove::push_wait_wave_time(double seconds) {
  CheckPoint cp;
  cp.type = CheckPoint::Type::WaitCurrentWaveTime;
  cp.wave_time_ticks = ticks_from_seconds_ceil(seconds);
  cps.push_back(cp);
}

void RouteMove::push_disappear_cp() {
  CheckPoint cp;
  cp.type = CheckPoint::Type::Disappear;
  cps.push_back(cp);
}

void RouteMove::push_appear_at_pos(vec<f32> cursor_pos) {
  CheckPoint cp;
  cp.type = CheckPoint::Type::AppearAtPos;
  cp.appear_cursor_pos = cursor_pos;
  cps.push_back(cp);
}

void RouteMove::push_alert_cp(std::uint32_t alert_id) {
  CheckPoint cp;
  cp.type = CheckPoint::Type::Alert;
  cp.alert_id = alert_id;
  cps.push_back(cp);
}

void RouteMove::push_wait_bossrush_wave(std::uint32_t wait_regions) {
  CheckPoint cp;
  cp.type = CheckPoint::Type::WaitBossrushWave;
  cp.wait_regions = wait_regions;
  cps.push_back(cp);
}

void RouteMove::set_end(TileCoord tile, vec<f32> point) {
  has_end = true;
  end_tile = tile;
  end_point = point;
}

void RouteMove::step(World& world, Entity self, Position& pos, const Map& map, PathMapCache& cache, Tick tick_rate) {
  if (world.has<Destroyed>(self)) {
    return;
  }
  if (!active) {
    return;
  }
  if (const auto* ub = world.try_get<Unbalance>(self); ub && ub->active) {
    return;
  }

  if (cps.empty()) {
    step_move_only(*this, world, self, pos, map, cache, tick_rate, target_tile, target_point);
    return;
  }

  game_time += tick_rate;
  fragment_time += tick_rate;
  wave_time += tick_rate;

  if (!reached_end) {
    // Pre-advance to avoid moving toward an already completed checkpoint.
    advance_checkpoints(*this, pos, map, cache);

    // Movement stage (only when current checkpoint is a move CP).
    if (runtime_state == RuntimeState::Move) {
      if (const auto* cp = current_cp(); cp && is_move_cp(cp->type)) {
        step_move_only(*this, world, self, pos, map, cache, tick_rate, cp->target_tile, cp->target_point);
      } else if (!cp && has_end) {
        step_move_only(*this, world, self, pos, map, cache, tick_rate, end_tile, end_point);
      }
    }

    // Update current checkpoint (timers etc).
    if (const auto* cp = current_cp()) {
      update_current_checkpoint(*this, *cp, tick_rate);
    }

    // Post-advance: allow skipping multiple CPs in a single tick.
    advance_checkpoints(*this, pos, map, cache);
  }

  if (!reached_end && has_end) {
    if (!visit_every_checkpoint || all_checkpoints_completed()) {
      const vec<f32> cursor_pos = pos.pos + cursor_offset;
      if (distance(cursor_pos, end_point) <= 0.05f) {
        reached_end = true;
      }
    }
  }
}

} // namespace arksim
