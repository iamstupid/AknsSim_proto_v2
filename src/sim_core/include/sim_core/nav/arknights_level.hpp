#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "sim_core/components/route_move.hpp"
#include "sim_core/core/vec.hpp"
#include "sim_core/nav/map.hpp"

namespace arksim {

class Rng;

struct ArknightsCheckpoint {
  RouteMove::CheckPoint::Type type = RouteMove::CheckPoint::Type::Move;

  // For MOVE/PATROL_MOVE/APPEAR_AT_POS checkpoints.
  TileCoord tile{};
  vec<f32> reach_offset{};
  bool randomize_reach_offset = false;
  f32 reach_distance = 0.0f;

  // For WAIT_* checkpoints.
  double time = 0.0;

  // ALERT / WAIT_BOSSRUSH_WAVE (optional; not fully wired in gameplay yet).
  std::uint32_t alert_id = 0;
  std::uint32_t wait_regions = 0;
};

struct ArknightsRoute {
  // Index-stable loader: ArknightsLevel.routes keeps the original JSON indices.
  // Unsupported/invalid routes are kept as invalid placeholders.
  bool valid = false;

  MoveMode mode = MoveMode::Ground;
  bool allow_diagonal_move = true;
  bool visit_every_checkpoint = false;
  f32 move_multiplier = 0.5f;

  TileCoord start_tile{};
  TileCoord end_tile{};

  vec<f32> spawn_offset{};
  vec<f32> spawn_random_range{};

  std::vector<ArknightsCheckpoint> checkpoints{};

  // Create a RouteMove instance for a spawned unit.
  // Randomization (e.g. randomizeReachOffset) is applied here using the provided RNG.
  RouteMove instantiate(Rng& rng) const;
};

struct ArknightsSpawnEvent {
  // Absolute stage time (ticks since stage start) at which the spawn should occur.
  Tick spawn_tick = 0;

  // For RouteMove timer initialization.
  Tick wave_start_tick = 0;
  Tick fragment_start_tick = 0;

  std::uint32_t wave_index = 0;
  std::uint32_t fragment_index = 0;
  std::uint32_t route_index = 0;

  // Enemy ID string, e.g. "enemy_1027_mob".
  std::string key{};
};

struct ArknightsLevel {
  Map map{};
  std::vector<ArknightsRoute> routes{};
  std::vector<ArknightsSpawnEvent> spawns{};

  int max_life_point = 0;
};

// Load an Arknights `level_*.json` file from ArknightsGameData.
// Coordinate system: x -> right, y -> up (so JSON `row` maps to TileCoord.y).
bool load_arknights_level_file(const std::filesystem::path& path,
                               ArknightsLevel& out,
                               std::string* error = nullptr);

} // namespace arksim
