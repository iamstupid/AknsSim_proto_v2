#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "sim_core/map.hpp"

namespace arksim {

// Terrain-independent cache of "cells crossed by a thick (width=0.4) segment" between tile centers.
//
// Thick segment = union of two shifted segments:
//   (0, +0.2) -> (dx, dy + 0.2)
//   (0, -0.2) -> (dx, dy - 0.2)
//
// Returned offsets are tile coords relative to the start tile (0,0), and include both endpoints.
class BresenhamCache {
public:
  const std::vector<TileCoord>& thick_line_offsets(int dx, int dy);
  const std::vector<TileCoord>& thick_line_offsets(TileCoord from, TileCoord to) { return thick_line_offsets(to.x - from.x, to.y - from.y); }

private:
  struct Key {
    int dx = 0;
    int dy = 0;
  };

  struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept;
  };

  struct KeyEq {
    bool operator()(const Key& a, const Key& b) const noexcept { return a.dx == b.dx && a.dy == b.dy; }
  };

  std::unordered_map<Key, std::vector<TileCoord>, KeyHash, KeyEq> cache_;

  static std::vector<TileCoord> trace_cells(const vec<f32>& start, const vec<f32>& end);
  static std::vector<TileCoord> build_thick_line(int dx, int dy);
};

} // namespace arksim

