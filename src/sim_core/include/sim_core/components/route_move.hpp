#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sim_core/ecs/ecs.hpp"
#include "sim_core/nav/map.hpp"
#include "sim_core/core/types.hpp"
#include "sim_core/core/vec.hpp"

namespace arksim {

struct Position;
class PathMapCache;

struct RouteMove {
  enum class RuntimeState : std::uint8_t {
    Move = 0,
    Disappeared = 1,
  };

  struct CheckPoint {
    enum class Type : std::uint8_t {
      Move = 0,
      PatrolMove = 1,
      WaitForSeconds = 2,
      WaitForPlayTime = 3,
      WaitCurrentFragmentTime = 4,
      WaitCurrentWaveTime = 5,

      // Route state/teleport points (minimal support for now).
      Disappear = 6,
      AppearAtPos = 7,
      Alert = 8,

      // Bossrush style wave gating.
      WaitBossrushWave = 9,
    };

    Type type = Type::Move;

    // MOVE / PATROL_MOVE
    TileCoord target_tile{};
    vec<f32> target_point{};
    f32 radius = 0.0f;

    // WAIT_FOR_SECONDS
    Tick wait_ticks = 0;

    // WAIT_FOR_PLAY_TIME / WAIT_CURRENT_FRAGMENT_TIME / WAIT_CURRENT_WAVE_TIME
    Tick play_time_ticks = 0;
    Tick fragment_time_ticks = 0;
    Tick wave_time_ticks = 0;

    // APPEAR_AT_POS
    vec<f32> appear_cursor_pos{};

    // ALERT
    std::uint32_t alert_id = 0;

    // WAIT_BOSSRUSH_WAVE
    std::uint32_t start_region = 0;
    std::uint32_t wait_regions = 0;
  };

  MoveMode mode = MoveMode::Ground;
  bool active = true;
  bool allow_diagonal_move = true;

  // cursorPos = entityPos + cursor_offset (fixed after spawn).
  vec<f32> cursor_offset{};
  vec<f32> foot_offset{0.0f, -0.2f};

  // Movement parameters.
  BuffNum move_speed{1.0};    // base move speed (buffable)
  f32 move_multiplier = 0.5f; // typically 0.5

  f32 steering_factor = 8.0f;
  f32 max_steering_force = 100.0f;

  // State.
  vec<f32> cached_avoid{};
  Tick avoid_remain = 0;

  bool is_bound = false;

  // Target for the current segment (for now: a single target).
  TileCoord target_tile{};
  vec<f32> target_point{};

  // Route runtime.
  RuntimeState runtime_state = RuntimeState::Move;
  bool visit_every_checkpoint = false;
  bool ignore_all_but_move_cp = false;

  std::vector<CheckPoint> cps{};
  std::uint32_t cp_index = 0;
  std::uint32_t entered_cp_index = static_cast<std::uint32_t>(-1);

  Tick game_time = 0;
  Tick fragment_time = 0;
  Tick wave_time = 0;
  Tick waited_ticks = 0;
  std::uint32_t region_index = 0;

  bool has_end = false;
  vec<f32> end_point{};
  TileCoord end_tile{};
  bool reached_end = false;

  void clear_route();
  void push_move_cp(TileCoord tile, vec<f32> point, f32 radius = 0.0f);
  void push_patrol_move_cp(TileCoord tile, vec<f32> point, f32 radius = 0.0f);
  void push_wait_seconds(double seconds);
  void push_wait_play_time(double seconds);
  void push_wait_fragment_time(double seconds);
  void push_wait_wave_time(double seconds);
  void push_disappear_cp();
  void push_appear_at_pos(vec<f32> cursor_pos);
  void push_alert_cp(std::uint32_t alert_id);
  void push_wait_bossrush_wave(std::uint32_t wait_regions);

  void set_end(TileCoord tile, vec<f32> point);

  bool all_checkpoints_completed() const { return cp_index >= cps.size(); }
  const CheckPoint* current_cp() const;
  CheckPoint* current_cp();

  void set_target(TileCoord tile, vec<f32> point) {
    target_tile = tile;
    target_point = point;
  }

  void step(World& world, Entity self, Position& pos, const Map& map, PathMapCache& cache, Tick tick_rate);
};

} // namespace arksim
