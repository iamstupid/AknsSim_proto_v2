#include "sim_core/nav/arknights_level.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
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

bool load_defined_double_opt(const nlohmann::json& attrs, const char* name, std::optional<double>& out) {
  if (!attrs.contains(name)) {
    return false;
  }
  const auto& node = attrs.at(name);
  if (!node.is_object()) {
    return false;
  }
  if (!node.value("m_defined", false)) {
    return false;
  }
  if (!node.contains("m_value")) {
    return false;
  }
  const auto& v = node.at("m_value");
  if (!v.is_number()) {
    return false;
  }
  out = v.get<double>();
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

constexpr Tick kTicksPerSecond = Tick{1} << 30;

Tick ticks_from_seconds_ceil(double seconds) {
  const long double scaled =
      std::ceil(static_cast<long double>(seconds) * static_cast<long double>(kTicksPerSecond));
  if (scaled <= 0.0L) {
    return 0;
  }
  const long double maxv = static_cast<long double>(std::numeric_limits<Tick>::max());
  if (scaled >= maxv) {
    return std::numeric_limits<Tick>::max();
  }
  return static_cast<Tick>(scaled);
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

  if (root.contains("options")) {
    const auto& opt = root.at("options");
    if (opt.contains("maxLifePoint")) {
      out.max_life_point = opt.at("maxLifePoint").get<int>();
    }
  }

  const nlohmann::json routes = root.contains("routes") ? root.at("routes") : nlohmann::json::array();
  if (!routes.is_array()) {
    if (error) {
      *error = "routes must be an array";
    }
    return false;
  }

  out.routes.clear();
  out.routes.resize(routes.size());
  for (std::size_t route_index = 0; route_index < routes.size(); ++route_index) {
    const auto& route_j = routes.at(route_index);
    ArknightsRoute& route = out.routes[route_index];
    route = ArknightsRoute{}; // reset placeholder (keeps index stable)

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

    route.valid = true;
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
  }

  // Enemy DB refs: stage-local overrides for enemy_database entries.
  out.enemy_db_refs.clear();
  const nlohmann::json enemy_db_refs = root.contains("enemyDbRefs") ? root.at("enemyDbRefs") : nlohmann::json::array();
  if (enemy_db_refs.is_array()) {
    for (const auto& ref_j : enemy_db_refs) {
      if (!ref_j.is_object()) {
        continue;
      }

      const bool use_db = ref_j.value("useDb", false);
      if (!use_db) {
        continue;
      }

      const std::string id = ref_j.value("id", "");
      if (id.empty()) {
        continue;
      }

      ArknightsEnemyRef ref;
      ref.id = id;
      ref.level = ref_j.value("level", 0);

      if (ref_j.contains("overwrittenData") && ref_j.at("overwrittenData").is_object()) {
        const auto& od = ref_j.at("overwrittenData");
        if (od.contains("attributes") && od.at("attributes").is_object()) {
          const auto& attrs = od.at("attributes");
          (void)load_defined_double_opt(attrs, "maxHp", ref.overridden.max_hp);
          (void)load_defined_double_opt(attrs, "atk", ref.overridden.atk);
          (void)load_defined_double_opt(attrs, "def", ref.overridden.def);

          std::optional<double> mr_percent;
          if (load_defined_double_opt(attrs, "magicResistance", mr_percent) && mr_percent.has_value()) {
            ref.overridden.magic_res = *mr_percent / 100.0;
          }

          (void)load_defined_double_opt(attrs, "moveSpeed", ref.overridden.move_speed);
          (void)load_defined_double_opt(attrs, "attackSpeed", ref.overridden.attack_speed);
          (void)load_defined_double_opt(attrs, "baseAttackTime", ref.overridden.base_attack_time);
        }
      }

      out.enemy_db_refs.push_back(std::move(ref));
    }
  }

  // Spawn schedule (waves -> fragments -> actions).
  // We currently flatten only SPAWN actions into `out.spawns`, but still account for
  // non-SPAWN actions when computing wave boundaries for sequential scheduling.
  out.spawns.clear();
  const nlohmann::json waves = root.contains("waves") ? root.at("waves") : nlohmann::json::array();
  if (waves.is_array()) {
    Tick cursor = 0;
    for (std::size_t wave_index = 0; wave_index < waves.size(); ++wave_index) {
      const auto& wave_j = waves.at(wave_index);
      if (!wave_j.is_object()) {
        continue;
      }

      const Tick wave_pre = ticks_from_seconds_ceil(wave_j.value("preDelay", 0.0));
      const Tick wave_post = ticks_from_seconds_ceil(wave_j.value("postDelay", 0.0));
      const Tick wave_start = cursor + wave_pre;
      Tick wave_end = wave_start;

      auto fragments = wave_j.value("fragments", nlohmann::json::array());
      if (fragments.is_null()) {
        fragments = nlohmann::json::array();
      }

      if (fragments.is_array()) {
        for (std::size_t frag_index = 0; frag_index < fragments.size(); ++frag_index) {
          const auto& frag_j = fragments.at(frag_index);
          if (!frag_j.is_object()) {
            continue;
          }

          const Tick frag_pre = ticks_from_seconds_ceil(frag_j.value("preDelay", 0.0));
          const Tick frag_start = wave_start + frag_pre;
          wave_end = std::max(wave_end, frag_start);

          auto actions = frag_j.value("actions", nlohmann::json::array());
          if (actions.is_null()) {
            actions = nlohmann::json::array();
          }

          if (!actions.is_array()) {
            continue;
          }

          for (std::size_t action_index = 0; action_index < actions.size(); ++action_index) {
            const auto& action_j = actions.at(action_index);
            if (!action_j.is_object()) {
              continue;
            }

            const std::string action_type = action_j.value("actionType", "");
            const int count_raw = action_j.value("count", 1);
            const std::uint32_t count = count_raw > 0 ? static_cast<std::uint32_t>(count_raw) : 0u;

            const Tick action_pre = ticks_from_seconds_ceil(action_j.value("preDelay", 0.0));
            const Tick interval = ticks_from_seconds_ceil(action_j.value("interval", 0.0));

            const Tick first_tick = frag_start + action_pre;
            Tick last_tick = first_tick;
            if (count > 1) {
              last_tick = first_tick + interval * static_cast<Tick>(count - 1);
            }
            wave_end = std::max(wave_end, last_tick);

            if (action_type != "SPAWN" || count == 0) {
              continue;
            }

            const std::string key = action_j.value("key", "");
            const int route_index_i = action_j.value("routeIndex", -1);
            if (route_index_i < 0) {
              continue;
            }
            const std::uint32_t route_index_u = static_cast<std::uint32_t>(route_index_i);
            if (route_index_u >= static_cast<std::uint32_t>(out.routes.size())) {
              continue;
            }

            for (std::uint32_t i = 0; i < count; ++i) {
              ArknightsSpawnEvent ev;
              ev.spawn_tick = first_tick + interval * static_cast<Tick>(i);
              ev.wave_start_tick = wave_start;
              ev.fragment_start_tick = frag_start;
              ev.wave_index = static_cast<std::uint32_t>(wave_index);
              ev.fragment_index = static_cast<std::uint32_t>(frag_index);
              ev.route_index = route_index_u;
              ev.key = key;
              out.spawns.push_back(std::move(ev));
            }
          }
        }
      }

      cursor = wave_end + wave_post;
    }

    std::stable_sort(out.spawns.begin(), out.spawns.end(), [](const ArknightsSpawnEvent& a, const ArknightsSpawnEvent& b) {
      return a.spawn_tick < b.spawn_tick;
    });
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

const ArknightsEnemyRef* ArknightsLevel::find_enemy_db_ref(std::string_view key) const {
  for (const auto& ref : enemy_db_refs) {
    if (ref.id == key) {
      return &ref;
    }
  }
  return nullptr;
}

} // namespace arksim
