#include "sim_core/nav/arknights_enemy_database.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace arksim {
namespace {

bool load_defined_double(const nlohmann::json& attrs, const char* name, double& out) {
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

bool load_defined_string(const nlohmann::json& obj, const char* name, std::string& out) {
  if (!obj.contains(name)) {
    return false;
  }
  const auto& node = obj.at(name);
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
  if (!v.is_string()) {
    return false;
  }
  out = v.get<std::string>();
  return true;
}

} // namespace

const ArknightsEnemyStats* ArknightsEnemyDatabase::find(std::string_view key, int level) const {
  const auto it = enemies.find(std::string(key));
  if (it == enemies.end()) {
    return nullptr;
  }

  const auto& levels = it->second;
  for (const auto& lv : levels) {
    if (lv.level == level) {
      return &lv.stats;
    }
  }

  return nullptr;
}

bool load_arknights_enemy_database_file(const std::filesystem::path& path,
                                       ArknightsEnemyDatabase& out,
                                       std::string* error) {
  out = ArknightsEnemyDatabase{};

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

  if (!root.contains("enemies") || !root.at("enemies").is_array()) {
    if (error) {
      *error = "Missing/invalid field: enemies (array)";
    }
    return false;
  }

  for (const auto& entry : root.at("enemies")) {
    if (!entry.is_object()) {
      continue;
    }

    const std::string key = entry.value("Key", "");
    if (key.empty()) {
      continue;
    }

    const nlohmann::json values = entry.contains("Value") ? entry.at("Value") : nlohmann::json::array();
    if (!values.is_array()) {
      continue;
    }

    std::vector<ArknightsEnemyLevel> levels;
    levels.reserve(values.size());

    for (const auto& v : values) {
      if (!v.is_object()) {
        continue;
      }

      ArknightsEnemyLevel lv;
      lv.level = v.value("level", 0);

      if (!v.contains("enemyData") || !v.at("enemyData").is_object()) {
        continue;
      }
      const auto& enemy_data = v.at("enemyData");
      if (!enemy_data.contains("attributes") || !enemy_data.at("attributes").is_object()) {
        continue;
      }
      const auto& attrs = enemy_data.at("attributes");

      std::string apply_way;
      if (load_defined_string(enemy_data, "applyWay", apply_way)) {
        if (apply_way == "MELEE") {
          lv.stats.apply_way = ArknightsEnemyStats::ApplyWay::Melee;
        } else if (apply_way == "RANGED") {
          lv.stats.apply_way = ArknightsEnemyStats::ApplyWay::Ranged;
        }
      }

      (void)load_defined_double(attrs, "maxHp", lv.stats.max_hp);
      (void)load_defined_double(attrs, "atk", lv.stats.atk);
      (void)load_defined_double(attrs, "def", lv.stats.def);

      double mr_percent = 0.0;
      if (load_defined_double(attrs, "magicResistance", mr_percent)) {
        lv.stats.magic_res = mr_percent / 100.0;
      }

      (void)load_defined_double(attrs, "moveSpeed", lv.stats.move_speed);
      (void)load_defined_double(attrs, "attackSpeed", lv.stats.attack_speed);
      (void)load_defined_double(attrs, "baseAttackTime", lv.stats.base_attack_time);

      double range_radius = 0.0;
      if (load_defined_double(enemy_data, "rangeRadius", range_radius)) {
        lv.stats.has_range_radius = true;
        lv.stats.range_radius = range_radius;
      }

      levels.push_back(std::move(lv));
    }

    if (!levels.empty()) {
      out.enemies.emplace(key, std::move(levels));
    }
  }

  return true;
}

} // namespace arksim
