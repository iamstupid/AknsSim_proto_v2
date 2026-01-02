#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arksim {

// Minimal enemy stat set extracted from ArknightsGameData `enemy_database.json`.
// Note: this is *not* a full fidelity schema; extend as needed.
struct ArknightsEnemyStats {
  enum class ApplyWay : std::uint8_t {
    Unknown = 0,
    Melee = 1,
    Ranged = 2,
  };

  ApplyWay apply_way = ApplyWay::Unknown;

  double max_hp = 0.0;
  double atk = 0.0;
  double def = 0.0;
  // Magic resistance as a fraction (0..1). Arknights data is usually stored as percent (0..100).
  double magic_res = 0.0;
  double move_speed = 1.0;
  // Attack speed percent (100 == normal).
  double attack_speed = 100.0;
  // Base attack interval in seconds.
  double base_attack_time = 0.0;

  // Base targeting range radius (tiles). When not set or <= 0, the engine uses defaults.
  bool has_range_radius = false;
  double range_radius = 0.0;
};

struct ArknightsEnemyLevel {
  int level = 0;
  ArknightsEnemyStats stats{};
};

struct ArknightsEnemyDatabase {
  std::unordered_map<std::string, std::vector<ArknightsEnemyLevel>> enemies{};

  const ArknightsEnemyStats* find(std::string_view key, int level = 0) const;
};

// Load ArknightsGameData `enemy_database.json`.
bool load_arknights_enemy_database_file(const std::filesystem::path& path,
                                       ArknightsEnemyDatabase& out,
                                       std::string* error = nullptr);

} // namespace arksim
