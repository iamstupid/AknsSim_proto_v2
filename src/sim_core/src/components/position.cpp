#include "sim_core/components/position.hpp"

#include <algorithm>
#include <cmath>

#include "sim_core/core/movement_utils.hpp"

namespace arksim {

namespace {

struct HomingContext {
  f32 dt = 0.0f;
  vec<f32> target_pos{};
  bool have_target_pos = false;
};

void normalize_dir(Position& self) {
  if (!(self.dir.length_sq() > 0.0f)) {
    self.dir = vec<f32>{1.0f, 0.0f};
    return;
  }
  self.dir.normalize();
  if (!(self.dir.length_sq() > 0.0f)) {
    self.dir = vec<f32>{1.0f, 0.0f};
  }
}

HomingContext prepare_homing(Position& self, World& world, Tick tick_rate) {
  HomingContext ctx;
  ctx.dt = tick_rate_to_seconds_f32(tick_rate);
  if (!(ctx.dt > 0.0f)) {
    return ctx;
  }

  normalize_dir(self);

  if (self.has_homing_target()) {
    if (const auto* target = world.try_get_idx_gen<Position>(self.homing_target_idx, self.homing_target_gen)) {
      ctx.target_pos = target->pos;
      ctx.have_target_pos = true;
    } else {
      self.clear_homing_target();
    }
  }

  if (ctx.have_target_pos && self.homing_angle_accel > 0.0f) {
    const vec<f32> diff = ctx.target_pos - self.pos;
    if (diff.length_sq() > 0.0f) {
      const vec<f32> homing_dir = diff.normalized();
      const f32 max_angle = self.homing_angle_accel * ctx.dt;
      if (max_angle > 0.0f) {
        const f32 cos_theta = dot(self.dir, homing_dir);
        const f32 min_cos = static_cast<f32>(std::cos(static_cast<double>(max_angle)));
        if (cos_theta > min_cos) {
          self.dir = homing_dir;
        } else {
          const f32 s = cross(self.dir, homing_dir);
          const f32 signed_angle = (s >= 0.0f) ? max_angle : -max_angle;
          self.dir = rotate(self.dir, signed_angle).normalized();
          if (!(self.dir.length_sq() > 0.0f)) {
            self.dir = homing_dir;
          }
        }
      }
    }
  }

  return ctx;
}

void maybe_snap_to_target(Position& self, const HomingContext& ctx) {
  if (!ctx.have_target_pos) {
    return;
  }
  const vec<f32> to_target = ctx.target_pos - self.pos;
  if (to_target.length() < self.approach_radius) {
    self.pos = ctx.target_pos;
  }
}

} // namespace

void Position::set_step_mode(StepMode mode) {
  step_mode = mode;
}

bool Position::has_homing_target() const {
  return homing_target_idx != ecs_lab::kInvalidIndex && homing_target_gen != 0;
}

void Position::clear_homing_target() {
  homing_target_idx = ecs_lab::kInvalidIndex;
  homing_target_gen = 0;
}

void Position::set_homing_target(Entity target) {
  homing_target_idx = target.entity_idx;
  homing_target_gen = target.gen;
}

void Position::step(World& world, Tick tick_rate) {
  switch (step_mode) {
    case StepMode::Normal:
      normal_step(world, tick_rate);
      break;
    case StepMode::Physical:
      physical_step(world, tick_rate);
      break;
    case StepMode::Stop:
      stop_step(world, tick_rate);
      break;
    default:
      step_mode = StepMode::Normal;
      normal_step(world, tick_rate);
      break;
  }
}

void Position::normal_step(World& world, Tick tick_rate) {
  HomingContext ctx = prepare_homing(*this, world, tick_rate);
  if (!(ctx.dt > 0.0f)) {
    return;
  }

  current_speed = std::min(current_speed, target_speed);
  pos += dir * (current_speed * ctx.dt);
  current_speed = std::min(target_speed, current_speed + ctx.dt * accel_speed);
  maybe_snap_to_target(*this, ctx);
}

void Position::physical_step(World& world, Tick tick_rate) {
  const f32 dt = tick_rate_to_seconds_f32(tick_rate);
  if (!(dt > 0.0f)) {
    return;
  }

  normalize_dir(*this);
  pos += dir * (current_speed * dt);
  current_speed = std::max(0.0f, current_speed + dt * accel_speed);
}

void Position::stop_step(World& world, Tick tick_rate) {
  (void)world;
  (void)tick_rate;
  current_speed = 0.0f;
}

} // namespace arksim
