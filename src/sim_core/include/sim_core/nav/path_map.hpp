#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

#include "sim_core/nav/bresenham_cache.hpp"
#include "sim_core/nav/map.hpp"

namespace arksim {

struct PathMap {
  static constexpr int kInf = std::numeric_limits<int>::max();

  int width = 0;
  int height = 0;
  MoveMode mode = MoveMode::Ground;
  TileCoord target{};

  std::vector<int> dist_to_target;
  std::vector<TileCoord> next_node_raw;
  std::vector<TileCoord> next_node_smooth;

  bool in_bounds(TileCoord t) const { return t.x >= 0 && t.x < width && t.y >= 0 && t.y < height; }
  std::size_t index(TileCoord t) const { return static_cast<std::size_t>(t.y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(t.x); }

  int dist(TileCoord t) const { return dist_to_target[index(t)]; }
  TileCoord next_raw(TileCoord t) const { return next_node_raw[index(t)]; }
  TileCoord next_smooth(TileCoord t) const { return next_node_smooth[index(t)]; }
};

// Cache of PathMap for a specific Map instance. Automatically invalidates on map.version() changes.
class PathMapCache {
public:
  PathMapCache(Map& map, BresenhamCache& bresenham) : map_(&map), bresenham_(&bresenham) {}

  // Flying units don't need PathMap: returns nullptr for MoveMode::Air.
  const PathMap* try_get(MoveMode mode, TileCoord target_tile, bool allow_diagonal_move);
  const PathMap& get_ground(TileCoord target_tile, bool allow_diagonal_move);

  void clear();

private:
  struct Key {
    TileCoord target{};
    bool allow_diagonal_move = false;
  };

  struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept;
  };

  struct KeyEq {
    bool operator()(const Key& a, const Key& b) const noexcept {
      return a.target == b.target && a.allow_diagonal_move == b.allow_diagonal_move;
    }
  };

  Map* map_ = nullptr;
  BresenhamCache* bresenham_ = nullptr;
  std::uint64_t cached_map_version_ = 0;

  std::unordered_map<Key, PathMap, KeyHash, KeyEq> ground_cache_;

  void invalidate_if_needed();
  PathMap build_ground(TileCoord target_tile, bool allow_diagonal_move) const;
  void smooth_next_nodes(PathMap& pm) const;
  bool line_clear_thick_ground(TileCoord a, TileCoord b) const;
};

} // namespace arksim

