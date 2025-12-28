#include "sim_core/path_map.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>

namespace arksim {

std::size_t PathMapCache::KeyHash::operator()(const Key& k) const noexcept {
  const std::uint64_t x = static_cast<std::uint32_t>(k.target.x);
  const std::uint64_t y = static_cast<std::uint32_t>(k.target.y);
  const std::uint64_t bits = (x << 32) ^ y ^ (k.allow_diagonal_move ? 0x9E3779B97F4A7C15ull : 0ull);
  return static_cast<std::size_t>(bits);
}

void PathMapCache::invalidate_if_needed() {
  assert(map_ != nullptr);
  const std::uint64_t v = map_->version();
  if (cached_map_version_ == v) {
    return;
  }
  clear();
  cached_map_version_ = v;
}

void PathMapCache::clear() {
  ground_cache_.clear();
}

const PathMap* PathMapCache::try_get(MoveMode mode, TileCoord target_tile, bool allow_diagonal_move) {
  invalidate_if_needed();

  if (mode == MoveMode::Air) {
    return nullptr;
  }
  return &get_ground(target_tile, allow_diagonal_move);
}

const PathMap& PathMapCache::get_ground(TileCoord target_tile, bool allow_diagonal_move) {
  invalidate_if_needed();
  const Key key{target_tile, allow_diagonal_move};
  auto it = ground_cache_.find(key);
  if (it != ground_cache_.end()) {
    return it->second;
  }

  auto [ins, ok] = ground_cache_.emplace(key, build_ground(target_tile, allow_diagonal_move));
  return ins->second;
}

PathMap PathMapCache::build_ground(TileCoord target_tile, bool allow_diagonal_move) const {
  assert(map_ != nullptr);

  PathMap pm;
  pm.width = map_->width();
  pm.height = map_->height();
  pm.mode = MoveMode::Ground;
  pm.target = target_tile;

  const std::size_t cell_count = static_cast<std::size_t>(pm.width) * static_cast<std::size_t>(pm.height);
  pm.dist_to_target.assign(cell_count, PathMap::kInf);
  pm.next_node_raw.resize(cell_count);
  pm.next_node_smooth.resize(cell_count);

  for (int y = 0; y < pm.height; ++y) {
    for (int x = 0; x < pm.width; ++x) {
      const TileCoord t{x, y};
      pm.next_node_raw[pm.index(t)] = t;
    }
  }

  if (!map_->in_bounds(target_tile)) {
    // Target outside map: everything unreachable.
    pm.next_node_smooth = pm.next_node_raw;
    return pm;
  }

  // Standard SPFA (reverse expansion from target).
  std::queue<TileCoord> q;
  std::vector<std::uint8_t> in_queue(cell_count, 0);

  pm.dist_to_target[pm.index(target_tile)] = 0;
  pm.next_node_raw[pm.index(target_tile)] = target_tile;
  q.push(target_tile);
  in_queue[pm.index(target_tile)] = 1;

  const TileCoord order[] = {
      TileCoord{0, 1},  // up
      TileCoord{1, 0},  // right
      TileCoord{0, -1}, // down
      TileCoord{-1, 0}, // left
  };

  while (!q.empty()) {
    const TileCoord cur = q.front();
    q.pop();
    in_queue[pm.index(cur)] = 0;

    const int cur_dist = pm.dist_to_target[pm.index(cur)];
    if (cur_dist == PathMap::kInf) {
      continue;
    }

    for (const TileCoord d : order) {
      const TileCoord nb{cur.x + d.x, cur.y + d.y};
      if (!pm.in_bounds(nb)) {
        continue;
      }

      // CanTraverse(nb -> cur): on ground, you must be able to stand on nb and enter cur.
      if (!map_->passable(nb, MoveMode::Ground) || !map_->passable(cur, MoveMode::Ground)) {
        continue;
      }

      const int penalty = map_->obstacle_penalty(nb, MoveMode::Ground);
      if (penalty == std::numeric_limits<int>::max()) {
        continue;
      }

      const std::int64_t cand64 = static_cast<std::int64_t>(cur_dist) + 1 + static_cast<std::int64_t>(penalty);
      const int cand = (cand64 >= static_cast<std::int64_t>(PathMap::kInf)) ? PathMap::kInf : static_cast<int>(cand64);

      const std::size_t ni = pm.index(nb);
      if (cand < pm.dist_to_target[ni]) {
        pm.dist_to_target[ni] = cand;
        pm.next_node_raw[ni] = cur;
        if (!in_queue[ni]) {
          q.push(nb);
          in_queue[ni] = 1;
        }
      }
    }
  }

  // Unpassable tiles: nextNode points to self (route_docs behavior).
  for (int y = 0; y < pm.height; ++y) {
    for (int x = 0; x < pm.width; ++x) {
      const TileCoord t{x, y};
      if (!map_->passable(t, MoveMode::Ground)) {
        pm.next_node_raw[pm.index(t)] = t;
        pm.dist_to_target[pm.index(t)] = PathMap::kInf;
      }
    }
  }

  pm.next_node_smooth = pm.next_node_raw;
  if (allow_diagonal_move) {
    smooth_next_nodes(pm);
  }
  return pm;
}

bool PathMapCache::line_clear_thick_ground(TileCoord a, TileCoord b) const {
  assert(map_ != nullptr);
  assert(bresenham_ != nullptr);

  const int dx = b.x - a.x;
  const int dy = b.y - a.y;
  const auto& offsets = bresenham_->thick_line_offsets(dx, dy);
  for (TileCoord off : offsets) {
    const TileCoord t{a.x + off.x, a.y + off.y};
    if (!map_->in_bounds(t)) {
      return false;
    }
    if (!map_->passable(t, MoveMode::Ground)) {
      return false;
    }
    const TileFlags f = map_->flags(t);
    if (has_flag(f, TileFlags::Obstacle) || has_flag(f, TileFlags::Hole)) {
      return false;
    }
  }
  return true;
}

void PathMapCache::smooth_next_nodes(PathMap& pm) const {
  assert(map_ != nullptr);
  assert(bresenham_ != nullptr);

  for (int y = 0; y < pm.height; ++y) {
    for (int x = 0; x < pm.width; ++x) {
      const TileCoord start{x, y};
      const std::size_t si = pm.index(start);
      if (pm.dist_to_target[si] == PathMap::kInf) {
        continue;
      }
      if (!map_->passable(start, MoveMode::Ground)) {
        continue;
      }

      TileCoord best = pm.next_node_smooth[si];
      if (best == start) {
        continue;
      }

      while (true) {
        const std::size_t bi = pm.index(best);
        const TileCoord next = pm.next_node_raw[bi];
        if (next == best || next == start) {
          break;
        }

        if (line_clear_thick_ground(start, next)) {
          best = next;
          continue;
        }
        break;
      }

      pm.next_node_smooth[si] = best;
    }
  }
}

} // namespace arksim

