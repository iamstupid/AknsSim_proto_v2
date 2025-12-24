#pragma once

#include <cstdint>

#include "sim_core/ecs.hpp"
#include "sim_core/types.hpp"
#include "sim_core/vec.hpp"

namespace arksim {

struct Position {
  enum class StepMode : std::uint8_t {
    Normal = 0,
    Physical = 1,
    Stop = 2,
  };

  vec<f32> pos{};
  vec<f32> dir{1.0f, 0.0f};

  f32 current_speed = 0.0f;
  f32 target_speed = 0.0f;
  f32 accel_speed = 0.0f;
  f32 approach_radius = 0.05f;

  std::uint32_t homing_target_idx = ecs_lab::kInvalidIndex;
  std::uint32_t homing_target_gen = 0;
  f32 homing_angle_accel = 0.0f; // radians per second, max turn per second

  StepMode step_mode = StepMode::Normal;

  void set_step_mode(StepMode mode);

  bool has_homing_target() const;
  void clear_homing_target();
  void set_homing_target(Entity target);

  void step(World& world, Tick tick_rate);
  void normal_step(World& world, Tick tick_rate);
  void physical_step(World& world, Tick tick_rate);
  void stop_step(World& world, Tick tick_rate);
};

} // namespace arksim
