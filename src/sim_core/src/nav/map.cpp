#include "sim_core/nav/map.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace arksim {

Map::Map(int width, int height) : width_(width), height_(height), tiles_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
  assert(width_ >= 0);
  assert(height_ >= 0);
}

bool Map::in_bounds(TileCoord t) const {
  return t.x >= 0 && t.x < width_ && t.y >= 0 && t.y < height_;
}

bool Map::in_world_bounds(const vec<f32>& p) const {
  if (width_ <= 0 || height_ <= 0) {
    return false;
  }
  const f32 min_x = -0.5f;
  const f32 min_y = -0.5f;
  const f32 max_x = static_cast<f32>(width_ - 1) + 0.5f;
  const f32 max_y = static_cast<f32>(height_ - 1) + 0.5f;
  return p.x >= min_x && p.x <= max_x && p.y >= min_y && p.y <= max_y;
}

vec<f32> Map::clamp_to_bounds(const vec<f32>& p) const {
  if (width_ <= 0 || height_ <= 0) {
    return p;
  }
  const f32 min_x = -0.5f;
  const f32 min_y = -0.5f;
  const f32 max_x = static_cast<f32>(width_ - 1) + 0.5f;
  const f32 max_y = static_cast<f32>(height_ - 1) + 0.5f;
  return vec<f32>{std::clamp(p.x, min_x, max_x), std::clamp(p.y, min_y, max_y)};
}

const Tile& Map::at(TileCoord t) const {
  assert(in_bounds(t));
  return tiles_[index(t)];
}

Tile& Map::at(TileCoord t) {
  assert(in_bounds(t));
  return tiles_[index(t)];
}

void Map::set_flags(TileCoord t, TileFlags flags) {
  Tile& tile = at(t);
  if (tile.flags == flags) {
    return;
  }
  tile.flags = flags;
  ++version_;
}

void Map::set_flag(TileCoord t, TileFlags flag, bool enabled) {
  Tile& tile = at(t);
  const TileFlags before = tile.flags;
  if (enabled) {
    tile.flags |= flag;
  } else {
    tile.flags &= ~flag;
  }
  if (tile.flags != before) {
    ++version_;
  }
}

bool Map::passable(TileCoord t, MoveMode mode) const {
  if (!in_bounds(t)) {
    return false;
  }
  if (mode == MoveMode::Air) {
    return true;
  }
  return !has_flag(flags(t), TileFlags::Unpassable);
}

int Map::obstacle_penalty(TileCoord t, MoveMode mode) const {
  if (!in_bounds(t) || mode == MoveMode::Air) {
    return 0;
  }
  const TileFlags f = flags(t);
  if (has_flag(f, TileFlags::Unpassable)) {
    return std::numeric_limits<int>::max();
  }
  if (has_flag(f, TileFlags::Hole)) {
    return 1'000'000;
  }
  if (has_flag(f, TileFlags::Obstacle)) {
    return 1000;
  }
  return 0;
}

int Map::round_half_to_even(double v) {
  const double f = std::floor(v);
  const double frac = v - f;

  if (frac < 0.5) {
    return static_cast<int>(f);
  }
  if (frac > 0.5) {
    return static_cast<int>(f + 1.0);
  }

  const std::int64_t i = static_cast<std::int64_t>(f);
  if ((i & 1) == 0) {
    return static_cast<int>(i);
  }
  return static_cast<int>(i + 1);
}

TileCoord Map::tile_at(const vec<f32>& pos) {
  return TileCoord{round_half_to_even(static_cast<double>(pos.x)), round_half_to_even(static_cast<double>(pos.y))};
}

Map::Snapshot Map::snapshot() const {
  Snapshot snap;
  snap.width = width_;
  snap.height = height_;
  snap.version = version_;
  snap.flags.reserve(tiles_.size());
  for (const Tile& t : tiles_) {
    snap.flags.push_back(t.flags);
  }
  return snap;
}

void Map::restore(const Snapshot& snap) {
  width_ = snap.width;
  height_ = snap.height;
  version_ = snap.version;

  const std::size_t expected = static_cast<std::size_t>(std::max(0, width_)) * static_cast<std::size_t>(std::max(0, height_));
  tiles_.clear();
  tiles_.resize(expected);

  if (snap.flags.size() != expected) {
    return;
  }

  for (std::size_t i = 0; i < expected; ++i) {
    tiles_[i].flags = snap.flags[i];
  }
}

} // namespace arksim
