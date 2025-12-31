#include "sim_core/spatial/spatial_grid.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "sim_core/components/destroyed.hpp"
#include "sim_core/components/position.hpp"

namespace arksim {

void SpatialGrid::reset(int width, int height) {
  width_ = width;
  height_ = height;
  cells_.clear();
  cells_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
}

void SpatialGrid::clear() {
  for (auto& bucket : cells_) {
    bucket.clear();
  }
}

void SpatialGrid::insert(TileCoord t, const SpatialEntry& entry) {
  assert(in_bounds(t));
  cells_[index(t)].push_back(entry);
}

const std::vector<SpatialEntry>& SpatialGrid::cell(TileCoord t) const {
  static const std::vector<SpatialEntry> kEmpty;
  if (!in_bounds(t)) {
    return kEmpty;
  }
  return cells_[index(t)];
}

namespace {

struct TileSquare {
  double min_x = 0;
  double max_x = 0;
  double min_y = 0;
  double max_y = 0;
};

TileSquare tile_square(TileCoord t) {
  const double x = static_cast<double>(t.x);
  const double y = static_cast<double>(t.y);
  return TileSquare{x - 0.5, x + 0.5, y - 0.5, y + 0.5};
}

bool circle_intersects_tile(const vec<f32>& center, double radius, TileCoord t) {
  const TileSquare s = tile_square(t);
  const double cx = static_cast<double>(center.x);
  const double cy = static_cast<double>(center.y);
  const double nx = std::clamp(cx, s.min_x, s.max_x);
  const double ny = std::clamp(cy, s.min_y, s.max_y);
  const double dx = cx - nx;
  const double dy = cy - ny;
  return dx * dx + dy * dy <= radius * radius;
}

bool rect_intersects_tile(const vec<f32>& center, double w, double h, TileCoord t) {
  const TileSquare s = tile_square(t);
  const double cx = static_cast<double>(center.x);
  const double cy = static_cast<double>(center.y);
  const double hx = w * 0.5;
  const double hy = h * 0.5;
  const double rmin_x = cx - hx;
  const double rmax_x = cx + hx;
  const double rmin_y = cy - hy;
  const double rmax_y = cy + hy;
  return rmax_x >= s.min_x && rmin_x <= s.max_x && rmax_y >= s.min_y && rmin_y <= s.max_y;
}

template <typename Fn>
void for_candidate_tiles_circle(const vec<f32>& center, double radius, int width, int height, Fn&& fn) {
  const double cx = static_cast<double>(center.x);
  const double cy = static_cast<double>(center.y);
  const int min_x = static_cast<int>(std::ceil(cx - radius - 0.5));
  const int max_x = static_cast<int>(std::floor(cx + radius + 0.5));
  const int min_y = static_cast<int>(std::ceil(cy - radius - 0.5));
  const int max_y = static_cast<int>(std::floor(cy + radius + 0.5));

  for (int y = min_y; y <= max_y; ++y) {
    for (int x = min_x; x <= max_x; ++x) {
      if (x < 0 || x >= width || y < 0 || y >= height) {
        continue;
      }
      fn(TileCoord{x, y});
    }
  }
}

template <typename Fn>
void for_candidate_tiles_rect(const vec<f32>& center, double w, double h, int width, int height, Fn&& fn) {
  const double cx = static_cast<double>(center.x);
  const double cy = static_cast<double>(center.y);
  const double hx = w * 0.5;
  const double hy = h * 0.5;

  const int min_x = static_cast<int>(std::ceil(cx - hx - 0.5));
  const int max_x = static_cast<int>(std::floor(cx + hx + 0.5));
  const int min_y = static_cast<int>(std::ceil(cy - hy - 0.5));
  const int max_y = static_cast<int>(std::floor(cy + hy + 0.5));

  for (int y = min_y; y <= max_y; ++y) {
    for (int x = min_x; x <= max_x; ++x) {
      if (x < 0 || x >= width || y < 0 || y >= height) {
        continue;
      }
      fn(TileCoord{x, y});
    }
  }
}

} // namespace

void SpatialIndex::rebuild(World& world) {
  occupation.clear();
  center.clear();

  auto bound_radius = [](const Area& area) -> f32 {
    if (area.type == Area::Type::Circle) {
      return std::max(area.radius.x, 0.0f);
    }
    const double w = std::max(0.0, static_cast<double>(area.radius.x));
    const double h = std::max(0.0, static_cast<double>(area.radius.y));
    const double hx = w * 0.5;
    const double hy = h * 0.5;
    return static_cast<f32>(std::sqrt(hx * hx + hy * hy));
  };

  world.query<Position, Area, Spatial>([&](Entity e, Position& pos, Area& area, Spatial& spatial) {
    if (world.has<Destroyed>(e)) {
      return;
    }
    SpatialEntry entry;
    entry.flags = spatial.flags;
    entry.entity = e;
    entry.center = pos.pos;
    entry.bound_radius = bound_radius(area);

    insert_occupation_(entry, area);
    insert_center_(entry, area);
  });
}

void SpatialIndex::insert_occupation_(const SpatialEntry& entry, const Area& area) {
  const int w = occupation.width();
  const int h = occupation.height();
  if (w <= 0 || h <= 0) {
    return;
  }

  if (area.type == Area::Type::Circle) {
    const double r = std::max(0.0, static_cast<double>(area.radius.x));
    for_candidate_tiles_circle(entry.center, r, w, h, [&](TileCoord t) {
      if (circle_intersects_tile(entry.center, r, t)) {
        occupation.insert(t, entry);
      }
    });
    return;
  }

  const double rw = std::max(0.0, static_cast<double>(area.radius.x));
  const double rh = std::max(0.0, static_cast<double>(area.radius.y));
  for_candidate_tiles_rect(entry.center, rw, rh, w, h, [&](TileCoord t) {
    if (rect_intersects_tile(entry.center, rw, rh, t)) {
      occupation.insert(t, entry);
    }
  });
}

void SpatialIndex::insert_center_(const SpatialEntry& entry, const Area& area) {
  const int w = center.width();
  const int h = center.height();
  if (w <= 0 || h <= 0) {
    return;
  }

  if (area.type == Area::Type::Circle) {
    const TileCoord t = Map::tile_at(entry.center); // banker's rounding
    if (center.in_bounds(t)) {
      center.insert(t, entry);
    }
    return;
  }

  // Rectangle: center grid is same as occupation grid.
  const double rw = std::max(0.0, static_cast<double>(area.radius.x));
  const double rh = std::max(0.0, static_cast<double>(area.radius.y));
  for_candidate_tiles_rect(entry.center, rw, rh, w, h, [&](TileCoord t) {
    if (rect_intersects_tile(entry.center, rw, rh, t)) {
      center.insert(t, entry);
    }
  });
}

} // namespace arksim
