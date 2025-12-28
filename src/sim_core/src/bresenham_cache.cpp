#include "sim_core/bresenham_cache.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace arksim {

std::size_t BresenhamCache::KeyHash::operator()(const Key& k) const noexcept {
  const std::uint64_t a = static_cast<std::uint32_t>(k.dx);
  const std::uint64_t b = static_cast<std::uint32_t>(k.dy);
  return static_cast<std::size_t>((a << 32) ^ b);
}

const std::vector<TileCoord>& BresenhamCache::thick_line_offsets(int dx, int dy) {
  const Key key{dx, dy};
  auto it = cache_.find(key);
  if (it != cache_.end()) {
    return it->second;
  }
  auto [ins, ok] = cache_.emplace(key, build_thick_line(dx, dy));
  return ins->second;
}

std::vector<TileCoord> BresenhamCache::build_thick_line(int dx, int dy) {
  const vec<f32> end{static_cast<f32>(dx), static_cast<f32>(dy)};

  std::vector<TileCoord> a = trace_cells(vec<f32>{0.0f, +0.2f}, vec<f32>{end.x, end.y + 0.2f});
  std::vector<TileCoord> b = trace_cells(vec<f32>{0.0f, -0.2f}, vec<f32>{end.x, end.y - 0.2f});

  std::vector<TileCoord> out;
  out.reserve(a.size() + b.size());

  auto key_of = [](TileCoord t) -> std::uint64_t {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.x)) << 32) |
           static_cast<std::uint32_t>(t.y);
  };

  std::unordered_set<std::uint64_t> seen;
  seen.reserve(a.size() + b.size());

  for (TileCoord t : a) {
    const std::uint64_t k = key_of(t);
    if (seen.insert(k).second) {
      out.push_back(t);
    }
  }
  for (TileCoord t : b) {
    const std::uint64_t k = key_of(t);
    if (seen.insert(k).second) {
      out.push_back(t);
    }
  }

  return out;
}

std::vector<TileCoord> BresenhamCache::trace_cells(const vec<f32>& start, const vec<f32>& end) {
  // Grid traversal on a unit grid whose cell centers are integers and boundaries are at +/-0.5.
  // Shift by +0.5 so boundaries become integers and we can use standard DDA traversal.
  const double x0 = static_cast<double>(start.x) + 0.5;
  const double y0 = static_cast<double>(start.y) + 0.5;
  const double x1 = static_cast<double>(end.x) + 0.5;
  const double y1 = static_cast<double>(end.y) + 0.5;

  int ix = static_cast<int>(std::floor(x0));
  int iy = static_cast<int>(std::floor(y0));
  const int ix_end = static_cast<int>(std::floor(x1));
  const int iy_end = static_cast<int>(std::floor(y1));

  std::vector<TileCoord> out;
  out.push_back(TileCoord{ix, iy});

  if (ix == ix_end && iy == iy_end) {
    return out;
  }

  const double dx = x1 - x0;
  const double dy = y1 - y0;

  const int step_x = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
  const int step_y = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;

  const double inf = std::numeric_limits<double>::infinity();
  const double t_delta_x = (step_x != 0) ? (1.0 / std::abs(dx)) : inf;
  const double t_delta_y = (step_y != 0) ? (1.0 / std::abs(dy)) : inf;

  const double next_boundary_x = (step_x > 0) ? static_cast<double>(ix + 1) : static_cast<double>(ix);
  const double next_boundary_y = (step_y > 0) ? static_cast<double>(iy + 1) : static_cast<double>(iy);

  double t_max_x = (step_x != 0) ? ((next_boundary_x - x0) / dx) : inf;
  double t_max_y = (step_y != 0) ? ((next_boundary_y - y0) / dy) : inf;

  // Make sure we progress even if we start exactly on a boundary.
  if (t_max_x < 0) {
    t_max_x = 0;
  }
  if (t_max_y < 0) {
    t_max_y = 0;
  }

  while (ix != ix_end || iy != iy_end) {
    if (t_max_x < t_max_y) {
      ix += step_x;
      t_max_x += t_delta_x;
      out.push_back(TileCoord{ix, iy});
      continue;
    }

    if (t_max_y < t_max_x) {
      iy += step_y;
      t_max_y += t_delta_y;
      out.push_back(TileCoord{ix, iy});
      continue;
    }

    // Corner crossing: include both adjacent cells (supercover).
    ix += step_x;
    t_max_x += t_delta_x;
    out.push_back(TileCoord{ix, iy});

    iy += step_y;
    t_max_y += t_delta_y;
    out.push_back(TileCoord{ix, iy});
  }

  return out;
}

} // namespace arksim

