#pragma once

#include <vector>

#include "sim_core/ecs/ecs.hpp"
#include "sim_core/nav/map.hpp"
#include "sim_core/core/types.hpp"
#include "sim_core/core/vec.hpp"

namespace arksim {

struct Position;

struct PullLink {
  bool active = false;
  vec<f32> source_pos{};
  f32 initial_distance = 0.0001f;
  f32 base_pull = 0.0f;
  Tick remain = 0;
};

// Push/pull (unbalance) simulation, implemented deterministically on top of Position velocity.
struct Unbalance {
  bool active = false;

  // NOTE: Flying units should not be affected by unbalance; keep Ground by default.
  MoveMode mode = MoveMode::Ground;

  // Avoidance parameters (shared behavior with RouteMove).
  vec<f32> foot_offset{0.0f, -0.2f};
  f32 half_body_width = 0.2f;
  f32 avoid_strength = 8.0f;
  f32 max_avoid_force = 100.0f;
  vec<f32> cached_avoid{};
  Tick avoid_remain = 0;

  // Unbalance state.
  Tick protect_remain = 0;
  f32 friction_force = 4.905f; // ~= 9.81 * 1.0 * 0.5
  f32 stop_speed = 0.1f;

  std::vector<PullLink> pulls{};

  void enter(Tick protect_ticks);
  void apply_impulse(Position& pos, vec<f32> dv, Tick protect_ticks);

  void clear_pulls();
  void add_pull(vec<f32> source_pos, f32 base_pull, f32 initial_distance, Tick duration_ticks);

  void step(World& world, Entity self, Position& pos, const Map& map, Tick tick_rate);
};

} // namespace arksim

