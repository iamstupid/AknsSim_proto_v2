#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <span>
#include <vector>

#include "sim_core/components/area.hpp"
#include "sim_core/components/spatial.hpp"
#include "sim_core/ecs/ecs.hpp"
#include "sim_core/nav/map.hpp"
#include "sim_core/core/types.hpp"
#include "sim_core/core/vec.hpp"

namespace arksim {

struct SpatialEntry {
  TypeFlags flags = TypeFlags::None;
  Entity entity{};
  vec<f32> center{};
  f32 bound_radius = 0.0f; // bounding circle radius for area-based queries
};

class SpatialGrid {
public:
  SpatialGrid() = default;
  SpatialGrid(int width, int height) { reset(width, height); }

  void reset(int width, int height);
  void clear();

  int width() const { return width_; }
  int height() const { return height_; }

  bool in_bounds(TileCoord t) const { return t.x >= 0 && t.x < width_ && t.y >= 0 && t.y < height_; }

  void insert(TileCoord t, const SpatialEntry& entry);

  const std::vector<SpatialEntry>& cell(TileCoord t) const;

  template <typename Fn>
  void query_tiles(std::span<const TileCoord> tiles, TypeFlags required, Fn&& fn) const {
    begin_query_();
    for (const TileCoord t : tiles) {
      if (!in_bounds(t)) {
        continue;
      }
      const auto& bucket = cells_[index(t)];
      for (const SpatialEntry& e : bucket) {
        if (!has_flags(e.flags, required)) {
          continue;
        }
        if (!mark_visited_(e.entity)) {
          continue;
        }
        fn(e);
      }
    }
  }

  void collect_tiles(std::span<const TileCoord> tiles, TypeFlags required, std::vector<SpatialEntry>& out) const {
    out.clear();
    query_tiles(tiles, required, [&](const SpatialEntry& e) { out.push_back(e); });
  }

  template <typename Fn>
  void query_circle(const vec<f32>& center, f32 radius, TypeFlags required, Fn&& fn) const {
    begin_query_();
    if (radius < 0) {
      return;
    }

    const double cx = static_cast<double>(center.x);
    const double cy = static_cast<double>(center.y);
    const double r = static_cast<double>(radius);
    const double r2 = r * r;

    int min_x = static_cast<int>(std::ceil(cx - r - 0.5));
    int max_x = static_cast<int>(std::floor(cx + r + 0.5));
    int min_y = static_cast<int>(std::ceil(cy - r - 0.5));
    int max_y = static_cast<int>(std::floor(cy + r + 0.5));

    min_x = std::max(min_x, 0);
    min_y = std::max(min_y, 0);
    max_x = std::min(max_x, width_ - 1);
    max_y = std::min(max_y, height_ - 1);

    if (min_x > max_x || min_y > max_y) {
      return;
    }

    for (int y = min_y; y <= max_y; ++y) {
      const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
      for (int x = min_x; x <= max_x; ++x) {
        // Tile square is [x-0.5, x+0.5] x [y-0.5, y+0.5].
        const double tx = static_cast<double>(x);
        const double ty = static_cast<double>(y);

        const double adx = std::abs(cx - tx);
        const double ady = std::abs(cy - ty);

        // Early exit: tile doesn't intersect circle.
        const double near_x = std::max(adx - 0.5, 0.0);
        const double near_y = std::max(ady - 0.5, 0.0);
        if (near_x * near_x + near_y * near_y > r2) {
          continue;
        }

        // If tile is fully contained, we can skip per-entry radius tests.
        const double far_x = adx + 0.5;
        const double far_y = ady + 0.5;
        const bool fully_covered = (far_x * far_x + far_y * far_y <= r2);

        const auto& bucket = cells_[row + static_cast<std::size_t>(x)];
        for (const SpatialEntry& e : bucket) {
          if (!has_flags(e.flags, required)) {
            continue;
          }
          if (!mark_visited_(e.entity)) {
            continue;
          }
          if (!fully_covered) {
            const double dx = static_cast<double>(e.center.x) - cx;
            const double dy = static_cast<double>(e.center.y) - cy;
            if (dx * dx + dy * dy > r2) {
              continue;
            }
          }
          fn(e);
        }
      }
    }
  }

  void collect_circle(const vec<f32>& center, f32 radius, TypeFlags required, std::vector<SpatialEntry>& out) const {
    out.clear();
    query_circle(center, radius, required, [&](const SpatialEntry& e) { out.push_back(e); });
  }

  template <typename Fn>
  void query_circle_intersect(const vec<f32>& center, f32 radius, TypeFlags required, Fn&& fn) const {
    begin_query_();
    if (radius < 0) {
      return;
    }

    const double cx = static_cast<double>(center.x);
    const double cy = static_cast<double>(center.y);
    const double r = static_cast<double>(radius);
    const double r2 = r * r;

    int min_x = static_cast<int>(std::ceil(cx - r - 0.5));
    int max_x = static_cast<int>(std::floor(cx + r + 0.5));
    int min_y = static_cast<int>(std::ceil(cy - r - 0.5));
    int max_y = static_cast<int>(std::floor(cy + r + 0.5));

    min_x = std::max(min_x, 0);
    min_y = std::max(min_y, 0);
    max_x = std::min(max_x, width_ - 1);
    max_y = std::min(max_y, height_ - 1);

    if (min_x > max_x || min_y > max_y) {
      return;
    }

    for (int y = min_y; y <= max_y; ++y) {
      const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
      for (int x = min_x; x <= max_x; ++x) {
        // Tile square is [x-0.5, x+0.5] x [y-0.5, y+0.5].
        const double tx = static_cast<double>(x);
        const double ty = static_cast<double>(y);

        const double adx = std::abs(cx - tx);
        const double ady = std::abs(cy - ty);

        // Early exit: tile doesn't intersect circle.
        const double near_x = std::max(adx - 0.5, 0.0);
        const double near_y = std::max(ady - 0.5, 0.0);
        if (near_x * near_x + near_y * near_y > r2) {
          continue;
        }

        // If tile is fully contained, we can skip per-entry radius tests.
        const double far_x = adx + 0.5;
        const double far_y = ady + 0.5;
        const bool fully_covered = (far_x * far_x + far_y * far_y <= r2);

        const auto& bucket = cells_[row + static_cast<std::size_t>(x)];
        for (const SpatialEntry& e : bucket) {
          if (!has_flags(e.flags, required)) {
            continue;
          }
          if (!mark_visited_(e.entity)) {
            continue;
          }
          if (!fully_covered) {
            const double rr = r + std::max(0.0, static_cast<double>(e.bound_radius));
            const double dx = static_cast<double>(e.center.x) - cx;
            const double dy = static_cast<double>(e.center.y) - cy;
            if (dx * dx + dy * dy > rr * rr) {
              continue;
            }
          }
          fn(e);
        }
      }
    }
  }

  void collect_circle_intersect(const vec<f32>& center,
                                f32 radius,
                                TypeFlags required,
                                std::vector<SpatialEntry>& out) const {
    out.clear();
    query_circle_intersect(center, radius, required, [&](const SpatialEntry& e) { out.push_back(e); });
  }

private:
  int width_ = 0;
  int height_ = 0;
  std::vector<std::vector<SpatialEntry>> cells_;
  mutable std::vector<std::uint32_t> visited_;
  mutable std::uint32_t stamp_ = 1;

  std::size_t index(TileCoord t) const {
    return static_cast<std::size_t>(t.y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(t.x);
  }

  void begin_query_() const {
    ++stamp_;
    if (stamp_ != 0) {
      return;
    }
    std::fill(visited_.begin(), visited_.end(), 0);
    stamp_ = 1;
  }

  bool mark_visited_(Entity e) const {
    if (e.entity_idx >= visited_.size()) {
      visited_.resize(static_cast<std::size_t>(e.entity_idx) + 1, 0);
    }
    std::uint32_t& slot = visited_[e.entity_idx];
    if (slot == stamp_) {
      return false;
    }
    slot = stamp_;
    return true;
  }
};

class SpatialIndex {
public:
  SpatialGrid occupation;
  SpatialGrid center;

  void reset(int width, int height) {
    occupation.reset(width, height);
    center.reset(width, height);
  }

  void rebuild(World& world);

private:
  void insert_occupation_(const SpatialEntry& entry, const Area& area);
  void insert_center_(const SpatialEntry& entry, const Area& area);
};

} // namespace arksim
