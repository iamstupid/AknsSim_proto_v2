#include "sim_core/components/unbalance.hpp"

#include <algorithm>

#include "sim_core/components/destroyed.hpp"
#include "sim_core/components/position.hpp"
#include "sim_core/components/route_move.hpp"
#include "sim_core/movement_utils.hpp"

namespace arksim {

namespace {

constexpr Tick kProtectInterval = (kTicksPerSecond + 9) / 10; // ceil(0.1s)
constexpr Tick kAvoidInterval = (kTicksPerSecond + 9) / 10;   // ceil(0.1s)

inline Tick tick_sub_sat(Tick a, Tick b) {
  if (a <= b) {
    return 0;
  }
  return a - b;
}

inline vec<f32> apply_friction(const vec<f32>& vel, f32 friction_force, f32 dt) {
  if (!(dt > 0.0f) || !(friction_force > 0.0f)) {
    return vel;
  }
  const f32 speed = vel.length();
  if (!(speed > 0.0f)) {
    return vel;
  }
  const f32 decel = friction_force * dt;
  if (!(decel > 0.0f)) {
    return vel;
  }
  if (decel >= speed) {
    return vec<f32>{};
  }
  return vel - (vel / speed) * decel;
}

inline vec<f32> pull_force(const PullLink& link, const vec<f32>& target_pos) {
  if (!link.active || !(link.base_pull > 0.0f)) {
    return vec<f32>{};
  }
  const vec<f32> diff = link.source_pos - target_pos;
  const f32 dist = diff.length();
  const f32 denom = std::max(link.initial_distance, 0.0001f);
  const f32 ratio = std::clamp(dist / denom, 0.0f, 1.0f);
  const f32 r2 = ratio * ratio;
  const f32 ease = r2 * r2; // ratio^4
  const f32 mag = ease * link.base_pull;
  return normalize_or_zero(diff) * mag;
}

} // namespace

void Unbalance::enter(Tick protect_ticks) {
  active = true;
  protect_remain = std::max(protect_remain, protect_ticks);
  if (protect_remain == 0) {
    protect_remain = kProtectInterval;
  }
}

void Unbalance::apply_impulse(Position& pos, vec<f32> dv, Tick protect_ticks) {
  enter(protect_ticks);
  vec<f32> vel = position_velocity(pos);
  vel += dv;
  set_position_velocity(pos, vel);
}

void Unbalance::clear_pulls() {
  pulls.clear();
}

void Unbalance::add_pull(vec<f32> source_pos, f32 base_pull, f32 initial_distance, Tick duration_ticks) {
  PullLink link;
  link.active = true;
  link.source_pos = source_pos;
  link.base_pull = base_pull;
  link.initial_distance = std::max(initial_distance, 0.0001f);
  link.remain = duration_ticks;
  pulls.push_back(link);
  enter(kProtectInterval);
}

void Unbalance::step(World& world, Entity self, Position& pos, const Map& map, Tick tick_rate) {
  if (!active) {
    return;
  }
  if (world.has<Destroyed>(self)) {
    return;
  }

  // Flying units are not affected by unbalance in the intended design.
  if (mode == MoveMode::Air) {
    active = false;
    protect_remain = 0;
    pulls.clear();
    return;
  }

  const f32 dt = tick_rate_to_seconds_f32(tick_rate);
  if (!(dt > 0.0f)) {
    return;
  }

  // Prefer RouteMove's offsets/mode if the component exists (even if inactive),
  // so unbalance motion stays consistent with cursor/foot semantics.
  vec<f32> cursor_offset{};
  vec<f32> foot_off = foot_offset;
  MoveMode move_mode = mode;
  if (const auto* rm = world.try_get<RouteMove>(self)) {
    cursor_offset = rm->cursor_offset;
    foot_off = rm->foot_offset;
    move_mode = rm->mode;
  }

  // 1) Update pull links and accumulate pull forces.
  vec<f32> sum_force{};
  std::size_t active_pulls = 0;
  for (auto& link : pulls) {
    if (!link.active) {
      continue;
    }
    if (link.remain != 0) {
      link.remain = tick_sub_sat(link.remain, tick_rate);
      if (link.remain == 0) {
        link.active = false;
        continue;
      }
    }
    ++active_pulls;
    sum_force += pull_force(link, pos.pos);
  }

  // 2) Velocity integration: friction + forces.
  vec<f32> vel = position_velocity(pos);
  vel = apply_friction(vel, friction_force, dt);
  vel += sum_force * dt; // mass=1 => dv = F * dt

  // 3) Avoidance (every ~0.1s), applied as an extra acceleration.
  const vec<f32> cursor_pos = pos.pos + cursor_offset;
  const vec<f32> given_dir = normalize_or_zero((vel.length_sq() > 0.0f) ? vel : sum_force);

  if (avoid_remain == 0 || avoid_remain <= tick_rate) {
    cached_avoid = compute_avoidance_force(map, move_mode, pos.pos, cursor_pos, foot_off, half_body_width, given_dir);
    avoid_remain = kAvoidInterval;
  } else {
    avoid_remain -= tick_rate;
  }

  {
    const f32 speed = vel.length();
    const f32 scale = std::max(speed, 0.5f);
    vec<f32> avoid_accel = cached_avoid * (avoid_strength * scale);
    avoid_accel = clamp_magnitude(avoid_accel, max_avoid_force);
    vel += avoid_accel * dt;
  }

  // 4) Apply displacement with impassable correction (reflect).
  const vec<f32> out_vel = apply_displacement_with_correction(map, move_mode, pos, vel * dt, dt);
  set_position_velocity(pos, out_vel);

  // 5) Protect timer and auto-exit when slow and not pulled.
  protect_remain = tick_sub_sat(protect_remain, tick_rate);

  if (protect_remain == 0 && active_pulls == 0 && !(pos.current_speed > stop_speed)) {
    active = false;
    set_position_velocity(pos, vec<f32>{});
    pulls.clear();
    cached_avoid = vec<f32>{};
    avoid_remain = 0;
  }
}

} // namespace arksim

