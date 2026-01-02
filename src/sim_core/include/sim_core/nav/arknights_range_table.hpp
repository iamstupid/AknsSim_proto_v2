#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sim_core/nav/map.hpp"

namespace arksim {

struct ArknightsRange {
  // Range ID, e.g. "1-1".
  std::string id{};

  // Relative tile offsets around the source, in Arknights coordinate convention:
  //   x = col, y = row (and the engine uses y-up).
  // These offsets are stored in the default orientation (facing +X / right).
  std::vector<TileCoord> offsets{};
};

struct ArknightsRangeTable {
  std::unordered_map<std::string, ArknightsRange> ranges{};

  const ArknightsRange* find(std::string_view id) const;
};

// Load ArknightsGameData `excel/range_table.json`.
bool load_arknights_range_table_file(const std::filesystem::path& path,
                                     ArknightsRangeTable& out,
                                     std::string* error = nullptr);

} // namespace arksim

