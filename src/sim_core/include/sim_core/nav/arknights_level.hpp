#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "sim_core/components/route_move.hpp"
#include "sim_core/core/vec.hpp"
#include "sim_core/nav/map.hpp"

namespace arksim {

struct ArknightsRoute {
  // A fully initialized RouteMove template (ready to be copied onto an enemy entity).
  RouteMove route{};

  // Spawn position for the route (cursor/world position, not entity pos).
  TileCoord start_tile{};
  vec<f32> start_point{};

  vec<f32> spawn_offset{};
  vec<f32> spawn_random_range{};
};

struct ArknightsLevel {
  Map map{};
  std::vector<ArknightsRoute> routes{};
};

// Load an Arknights `level_*.json` file from ArknightsGameData.
// Coordinate system: x -> right, y -> up (so JSON `row` maps to TileCoord.y).
bool load_arknights_level_file(const std::filesystem::path& path,
                               ArknightsLevel& out,
                               std::string* error = nullptr,
                               std::uint64_t random_seed = 0);

} // namespace arksim
