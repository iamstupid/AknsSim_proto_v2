#include "sim_core/nav/arknights_range_table.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace arksim {

const ArknightsRange* ArknightsRangeTable::find(std::string_view id) const {
  const auto it = ranges.find(std::string(id));
  if (it == ranges.end()) {
    return nullptr;
  }
  return &it->second;
}

bool load_arknights_range_table_file(const std::filesystem::path& path,
                                     ArknightsRangeTable& out,
                                     std::string* error) {
  out = ArknightsRangeTable{};

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

  if (!root.is_object()) {
    if (error) {
      *error = "range_table root must be an object";
    }
    return false;
  }

  for (auto it = root.begin(); it != root.end(); ++it) {
    const std::string key = it.key();
    const auto& entry = it.value();
    if (!entry.is_object()) {
      continue;
    }

    ArknightsRange range;
    range.id = entry.value("id", key);

    const auto grids = entry.contains("grids") ? entry.at("grids") : nlohmann::json::array();
    if (!grids.is_array()) {
      continue;
    }

    range.offsets.clear();
    range.offsets.reserve(grids.size());
    for (const auto& g : grids) {
      if (!g.is_object()) {
        continue;
      }
      const int row = g.value("row", 0);
      const int col = g.value("col", 0);
      range.offsets.push_back(TileCoord{col, row});
    }

    out.ranges.emplace(range.id, std::move(range));
  }

  return true;
}

} // namespace arksim

