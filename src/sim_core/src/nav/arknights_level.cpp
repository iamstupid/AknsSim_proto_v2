#include "sim_core/nav/arknights_level.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace arksim {
namespace {

std::string lower_ascii(std::string s) {
  for (char& ch : s) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return s;
}

bool parse_vec2(const nlohmann::json& j, vec<f32>& out) {
  if (!j.is_object()) {
    out = vec<f32>{};
    return false;
  }
  if (!j.contains("x") || !j.contains("y")) {
    out = vec<f32>{};
    return false;
  }
  out.x = static_cast<f32>(j.at("x").get<double>());
  out.y = static_cast<f32>(j.at("y").get<double>());
  return true;
}

std::uint64_t splitmix64(std::uint64_t& state) {
  std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

f32 unit_f32_from_u64(std::uint64_t x) {
  // Use top 24 bits to build a float in [0, 1).
  constexpr f32 kInv = 1.0f / static_cast<f32>(1u << 24);
  const std::uint32_t top24 = static_cast<std::uint32_t>(x >> 40);
  return static_cast<f32>(top24) * kInv;
}

TileFlags tile_flags_from_ak_tile(const nlohmann::json& tile) {
  TileFlags flags = TileFlags::None;

  const std::string tile_key = tile.value("tileKey", "");
  const std::string buildable = tile.value("buildableType", "NONE");
  const std::string passable = tile.value("passableMask", "ALL");

  if (buildable == "MELEE" || buildable == "ALL") {
    flags |= TileFlags::MeleeDeployable;
  }
  if (buildable == "RANGED" || buildable == "ALL") {
    flags |= TileFlags::RangedDeployable;
  }

  if (passable == "FLY_ONLY") {
    flags |= TileFlags::Unpassable;
  }

  const std::string key_l = lower_ascii(tile_key);
  if (key_l.find("hole") != std::string::npos) {
    flags |= TileFlags::Hole;
  }

  // Heuristic mapping for tiles that should be strongly avoided but are still passable.
  // (Some dynamic obstacles like boxes are represented outside tiles; this is a best-effort fallback.)
  if (key_l.find("obstacle") != std::string::npos || key_l.find("roadblock") != std::string::npos ||
      key_l.find("block") != std::string::npos) {
    flags |= TileFlags::Obstacle;
  }

  return flags;
}

bool parse_checkpoint_type(std::string_view s, RouteMove::CheckPoint::Type& out) {
  using Type = RouteMove::CheckPoint::Type;

  if (s == "MOVE") {
    out = Type::Move;
    return true;
  }
  if (s == "PATROL_MOVE") {
    out = Type::PatrolMove;
    return true;
  }
  if (s == "WAIT_FOR_SECONDS") {
    out = Type::WaitForSeconds;
    return true;
  }
  if (s == "WAIT_FOR_PLAY_TIME") {
    out = Type::WaitForPlayTime;
    return true;
  }
  if (s == "WAIT_CURRENT_FRAGMENT_TIME") {
    out = Type::WaitCurrentFragmentTime;
    return true;
  }
  if (s == "WAIT_CURRENT_WAVE_TIME") {
    out = Type::WaitCurrentWaveTime;
    return true;
  }
  if (s == "DISAPPEAR") {
    out = Type::Disappear;
    return true;
  }
  if (s == "APPEAR_AT_POS") {
    out = Type::AppearAtPos;
    return true;
  }
  if (s == "ALERT") {
    out = Type::Alert;
    return true;
  }
  if (s == "WAIT_BOSSRUSH_WAVE") {
    out = Type::WaitBossrushWave;
    return true;
  }

  return false;
}

} // namespace

bool load_arknights_level_file(const std::filesystem::path& path,
                               ArknightsLevel& out,
                               std::string* error,
                               std::uint64_t random_seed) {
  out = ArknightsLevel{};

  std::ifstream file(path);
  if (!file) {
    if (error) {
      *error = "Failed to open file: " + path.string();
    }
    return false;
  }

  nlohmann::json root;
  try {
    file >> root;
  } catch (const std::exception& e) {
    if (error) {
      *error = std::string("JSON parse error: ") + e.what();
    }
    return false;
  }

  if (!root.contains("mapData")) {
    if (error) {
      *error = "Missing field: mapData";
    }
    return false;
  }

  const auto& map_data = root.at("mapData");
  if (!map_data.contains("map") || !map_data.contains("tiles")) {
    if (error) {
      *error = "Missing field in mapData: map/tiles";
    }
    return false;
  }

  const auto& map_rows = map_data.at("map");
  const auto& tiles = map_data.at("tiles");
  if (!map_rows.is_array() || !tiles.is_array()) {
    if (error) {
      *error = "mapData.map and mapData.tiles must be arrays";
    }
    return false;
  }

  const int height = static_cast<int>(map_rows.size());
  if (height <= 0) {
    if (error) {
      *error = "mapData.map is empty";
    }
    return false;
  }

  const int width = static_cast<int>(map_rows.at(0).size());
  if (width <= 0) {
    if (error) {
      *error = "mapData.map[0] is empty";
    }
    return false;
  }

  for (int r = 0; r < height; ++r) {
    if (!map_rows.at(r).is_array() || static_cast<int>(map_rows.at(r).size()) != width) {
      if (error) {
        *error = "mapData.map rows must be arrays with consistent width";
      }
      return false;
    }
  }

  Map map(width, height);

  // The `mapData.map` rows are stored from top to bottom, while the simulation uses y-up coordinates.
  // So we map JSON row-index `r` to tile y = (height - 1 - r).
  for (int r = 0; r < height; ++r) {
    const int y = height - 1 - r;
    for (int x = 0; x < width; ++x) {
      const int tile_idx = map_rows.at(r).at(x).get<int>();
      if (tile_idx < 0 || tile_idx >= static_cast<int>(tiles.size())) {
        if (error) {
          *error = "mapData.map references invalid tile index";
        }
        return false;
      }
      const auto& tile = tiles.at(tile_idx);
      map.at(TileCoord{x, y}).flags = tile_flags_from_ak_tile(tile);
    }
  }

  out.map = std::move(map);

  const f32 move_multiplier = [&]() -> f32 {
    if (root.contains("options")) {
      const auto& opt = root.at("options");
      if (opt.contains("moveMultiplier")) {
        return static_cast<f32>(opt.at("moveMultiplier").get<double>());
      }
    }
    return 0.5f;
  }();

  const nlohmann::json routes = root.contains("routes") ? root.at("routes") : nlohmann::json::array();
  if (!routes.is_array()) {
    if (error) {
      *error = "routes must be an array";
    }
    return false;
  }

  out.routes.reserve(routes.size());
  for (std::size_t route_index = 0; route_index < routes.size(); ++route_index) {
    const auto& route_j = routes.at(route_index);
    if (!route_j.is_object()) {
      continue;
    }

    const std::string motion = route_j.value("motionMode", "WALK");
    MoveMode mode = MoveMode::Ground;
    if (motion == "FLY") {
      mode = MoveMode::Air;
    }

    if (!route_j.contains("startPosition") || !route_j.contains("endPosition")) {
      continue;
    }

    const auto& sp = route_j.at("startPosition");
    const auto& ep = route_j.at("endPosition");
    if (!sp.is_object() || !ep.is_object()) {
      continue;
    }

    const int start_row = sp.value("row", 0);
    const int start_col = sp.value("col", 0);
    const int end_row = ep.value("row", 0);
    const int end_col = ep.value("col", 0);

    const TileCoord start_tile{start_col, start_row};
    const TileCoord end_tile{end_col, end_row};
    if (!out.map.in_bounds(start_tile) || !out.map.in_bounds(end_tile)) {
      continue;
    }

    vec<f32> spawn_offset{};
    vec<f32> spawn_random_range{};
    (void)parse_vec2(route_j.value("spawnOffset", nlohmann::json{}), spawn_offset);
    (void)parse_vec2(route_j.value("spawnRandomRange", nlohmann::json{}), spawn_random_range);

    RouteMove rm;
    rm.clear_route();
    rm.mode = mode;
    rm.move_multiplier = move_multiplier;
    rm.allow_diagonal_move = route_j.value("allowDiagonalMove", rm.allow_diagonal_move);
    rm.visit_every_checkpoint = route_j.value("visitEveryCheckPoint", rm.visit_every_checkpoint);

    rm.set_end(end_tile, Map::tile_center(end_tile));

    auto checkpoints = route_j.value("checkpoints", nlohmann::json::array());
    if (checkpoints.is_null()) {
      checkpoints = nlohmann::json::array();
    }
    if (checkpoints.is_array()) {
      for (std::size_t checkpoint_index = 0; checkpoint_index < checkpoints.size(); ++checkpoint_index) {
        const auto& cp_j = checkpoints.at(checkpoint_index);
        if (!cp_j.is_object()) {
          continue;
        }

        RouteMove::CheckPoint::Type type{};
        const std::string type_s = cp_j.value("type", "");
        if (!parse_checkpoint_type(type_s, type)) {
          continue;
        }

        const double time = cp_j.value("time", 0.0);

        switch (type) {
          case RouteMove::CheckPoint::Type::Move:
          case RouteMove::CheckPoint::Type::PatrolMove: {
            if (!cp_j.contains("position") || !cp_j.at("position").is_object()) {
              continue;
            }
            const auto& pos = cp_j.at("position");
            const int row = pos.value("row", 0);
            const int col = pos.value("col", 0);
            const TileCoord t{col, row};
            if (!out.map.in_bounds(t)) {
              continue;
            }

            vec<f32> reach_offset{};
            (void)parse_vec2(cp_j.value("reachOffset", nlohmann::json{}), reach_offset);
            const bool randomize_reach_offset = cp_j.value("randomizeReachOffset", false);
            if (randomize_reach_offset) {
              // Deterministic per (seed, routeIndex, checkpointIndex).
              // Treat reachOffset as the max extents; sample uniformly within [-abs(x), +abs(x)] etc.
              const f32 rx = static_cast<f32>(std::abs(static_cast<double>(reach_offset.x)));
              const f32 ry = static_cast<f32>(std::abs(static_cast<double>(reach_offset.y)));

              std::uint64_t s = random_seed;
              s ^= 0xA3B195354A39B70Dull;
              s ^= static_cast<std::uint64_t>(route_index) * 0x9E3779B97F4A7C15ull;
              s ^= static_cast<std::uint64_t>(checkpoint_index) * 0xBF58476D1CE4E5B9ull;
              s ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.x)) * 0x94D049BB133111EBull;
              s ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.y)) * 0xD6E8FEB86659FD93ull;

              const f32 ux = unit_f32_from_u64(splitmix64(s));
              const f32 uy = unit_f32_from_u64(splitmix64(s));

              reach_offset.x = (ux * 2.0f - 1.0f) * rx;
              reach_offset.y = (uy * 2.0f - 1.0f) * ry;
            }
            const f32 reach_distance = static_cast<f32>(cp_j.value("reachDistance", 0.0));
            const vec<f32> point = Map::tile_center(t) + reach_offset;

            if (type == RouteMove::CheckPoint::Type::Move) {
              rm.push_move_cp(t, point, reach_distance);
            } else {
              rm.push_patrol_move_cp(t, point, reach_distance);
            }
            break;
          }
          case RouteMove::CheckPoint::Type::WaitForSeconds:
            rm.push_wait_seconds(time);
            break;
          case RouteMove::CheckPoint::Type::WaitForPlayTime:
            rm.push_wait_play_time(time);
            break;
          case RouteMove::CheckPoint::Type::WaitCurrentFragmentTime:
            rm.push_wait_fragment_time(time);
            break;
          case RouteMove::CheckPoint::Type::WaitCurrentWaveTime:
            rm.push_wait_wave_time(time);
            break;
          case RouteMove::CheckPoint::Type::Disappear:
            rm.push_disappear_cp();
            break;
          case RouteMove::CheckPoint::Type::AppearAtPos: {
            if (!cp_j.contains("position") || !cp_j.at("position").is_object()) {
              continue;
            }
            const auto& pos = cp_j.at("position");
            const int row = pos.value("row", 0);
            const int col = pos.value("col", 0);
            const TileCoord t{col, row};
            if (!out.map.in_bounds(t)) {
              continue;
            }
            vec<f32> reach_offset{};
            (void)parse_vec2(cp_j.value("reachOffset", nlohmann::json{}), reach_offset);
            const bool randomize_reach_offset = cp_j.value("randomizeReachOffset", false);
            if (randomize_reach_offset) {
              const f32 rx = static_cast<f32>(std::abs(static_cast<double>(reach_offset.x)));
              const f32 ry = static_cast<f32>(std::abs(static_cast<double>(reach_offset.y)));

              std::uint64_t s = random_seed;
              s ^= 0x0D6D6E9E7DABAE6Full;
              s ^= static_cast<std::uint64_t>(route_index) * 0x9E3779B97F4A7C15ull;
              s ^= static_cast<std::uint64_t>(checkpoint_index) * 0xBF58476D1CE4E5B9ull;
              s ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.x)) * 0x94D049BB133111EBull;
              s ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.y)) * 0xD6E8FEB86659FD93ull;

              const f32 ux = unit_f32_from_u64(splitmix64(s));
              const f32 uy = unit_f32_from_u64(splitmix64(s));

              reach_offset.x = (ux * 2.0f - 1.0f) * rx;
              reach_offset.y = (uy * 2.0f - 1.0f) * ry;
            }
            rm.push_appear_at_pos(Map::tile_center(t) + reach_offset);
            break;
          }
          case RouteMove::CheckPoint::Type::Alert:
            rm.push_alert_cp(static_cast<std::uint32_t>(cp_j.value("alertId", 0)));
            break;
          case RouteMove::CheckPoint::Type::WaitBossrushWave:
            rm.push_wait_bossrush_wave(static_cast<std::uint32_t>(cp_j.value("waitRegions", 0)));
            break;
          default:
            break;
        }
      }
    }

    const vec<f32> start_point = Map::tile_center(start_tile) + spawn_offset;

    ArknightsRoute r;
    r.route = std::move(rm);
    r.start_tile = start_tile;
    r.start_point = start_point;
    r.spawn_offset = spawn_offset;
    r.spawn_random_range = spawn_random_range;
    out.routes.push_back(std::move(r));
  }

  return true;
}

} // namespace arksim
