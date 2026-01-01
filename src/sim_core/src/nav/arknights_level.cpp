#include "sim_core/nav/arknights_level.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string_view>

#include <nlohmann/json.hpp>

#include "sim_core/core/rng.hpp"

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
                               std::string* error) {
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
    } else if (motion != "WALK") {
      // Unknown/unsupported motion mode for now.
      continue;
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

    ArknightsRoute route;
    route.mode = mode;
    route.move_multiplier = move_multiplier;
    route.allow_diagonal_move = route_j.value("allowDiagonalMove", route.allow_diagonal_move);
    route.visit_every_checkpoint = route_j.value("visitEveryCheckPoint", route.visit_every_checkpoint);
    route.start_tile = start_tile;
    route.end_tile = end_tile;
    route.spawn_offset = spawn_offset;
    route.spawn_random_range = spawn_random_range;

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
            const f32 reach_distance = static_cast<f32>(cp_j.value("reachDistance", 0.0));

            ArknightsCheckpoint cp;
            cp.type = type;
            cp.tile = t;
            cp.reach_offset = reach_offset;
            cp.randomize_reach_offset = randomize_reach_offset;
            cp.reach_distance = reach_distance;
            route.checkpoints.push_back(std::move(cp));
            break;
          }
          case RouteMove::CheckPoint::Type::WaitForSeconds:
          case RouteMove::CheckPoint::Type::WaitForPlayTime:
          case RouteMove::CheckPoint::Type::WaitCurrentFragmentTime:
          case RouteMove::CheckPoint::Type::WaitCurrentWaveTime: {
            ArknightsCheckpoint cp;
            cp.type = type;
            cp.time = time;
            route.checkpoints.push_back(std::move(cp));
            break;
          }
          case RouteMove::CheckPoint::Type::Disappear: {
            ArknightsCheckpoint cp;
            cp.type = type;
            route.checkpoints.push_back(std::move(cp));
            break;
          }
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

            ArknightsCheckpoint cp;
            cp.type = type;
            cp.tile = t;
            cp.reach_offset = reach_offset;
            cp.randomize_reach_offset = randomize_reach_offset;
            route.checkpoints.push_back(std::move(cp));
            break;
          }
          case RouteMove::CheckPoint::Type::Alert: {
            ArknightsCheckpoint cp;
            cp.type = type;
            cp.alert_id = static_cast<std::uint32_t>(cp_j.value("alertId", 0));
            route.checkpoints.push_back(std::move(cp));
            break;
          }
          case RouteMove::CheckPoint::Type::WaitBossrushWave: {
            ArknightsCheckpoint cp;
            cp.type = type;
            cp.wait_regions = static_cast<std::uint32_t>(cp_j.value("waitRegions", 0));
            route.checkpoints.push_back(std::move(cp));
            break;
          }
          default:
            break;
        }
      }
    }

    out.routes.push_back(std::move(route));
  }

  return true;
}

RouteMove ArknightsRoute::instantiate(Rng& rng) const {
  RouteMove rm;
  rm.clear_route();
  rm.mode = mode;
  rm.allow_diagonal_move = allow_diagonal_move;
  rm.visit_every_checkpoint = visit_every_checkpoint;
  rm.move_multiplier = move_multiplier;
  rm.set_end(end_tile, Map::tile_center(end_tile));

  for (const ArknightsCheckpoint& cp : checkpoints) {
    auto sample_offset = [&]() -> vec<f32> {
      if (!cp.randomize_reach_offset) {
        return cp.reach_offset;
      }

      const f32 rx = std::abs(cp.reach_offset.x);
      const f32 ry = std::abs(cp.reach_offset.y);
      return vec<f32>{rng.uniform_f32(-rx, rx), rng.uniform_f32(-ry, ry)};
    };

    switch (cp.type) {
      case RouteMove::CheckPoint::Type::Move: {
        const vec<f32> point = Map::tile_center(cp.tile) + sample_offset();
        rm.push_move_cp(cp.tile, point, cp.reach_distance);
        break;
      }
      case RouteMove::CheckPoint::Type::PatrolMove: {
        const vec<f32> point = Map::tile_center(cp.tile) + sample_offset();
        rm.push_patrol_move_cp(cp.tile, point, cp.reach_distance);
        break;
      }
      case RouteMove::CheckPoint::Type::WaitForSeconds:
        rm.push_wait_seconds(cp.time);
        break;
      case RouteMove::CheckPoint::Type::WaitForPlayTime:
        rm.push_wait_play_time(cp.time);
        break;
      case RouteMove::CheckPoint::Type::WaitCurrentFragmentTime:
        rm.push_wait_fragment_time(cp.time);
        break;
      case RouteMove::CheckPoint::Type::WaitCurrentWaveTime:
        rm.push_wait_wave_time(cp.time);
        break;
      case RouteMove::CheckPoint::Type::Disappear:
        rm.push_disappear_cp();
        break;
      case RouteMove::CheckPoint::Type::AppearAtPos: {
        const vec<f32> cursor_pos = Map::tile_center(cp.tile) + sample_offset();
        rm.push_appear_at_pos(cursor_pos);
        break;
      }
      case RouteMove::CheckPoint::Type::Alert:
        rm.push_alert_cp(cp.alert_id);
        break;
      case RouteMove::CheckPoint::Type::WaitBossrushWave:
        rm.push_wait_bossrush_wave(cp.wait_regions);
        break;
      default:
        break;
    }
  }

  return rm;
}

} // namespace arksim
