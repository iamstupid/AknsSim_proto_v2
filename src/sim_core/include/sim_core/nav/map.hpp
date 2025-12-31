#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "sim_core/core/types.hpp"
#include "sim_core/core/vec.hpp"

namespace arksim {

enum class MoveMode : std::uint8_t { Ground, Air };

struct TileCoord {
  int x = 0;
  int y = 0;

  constexpr bool operator==(const TileCoord& other) const { return x == other.x && y == other.y; }
  constexpr bool operator!=(const TileCoord& other) const { return !(*this == other); }
};

enum class TileFlags : std::uint32_t {
  None = 0,

  MeleeDeployable = 1u << 0,
  RangedDeployable = 1u << 1,

  Obstacle = 1u << 2,   // +1000 path penalty (ground)
  Unpassable = 1u << 3, // INF distance (ground)
  Hole = 1u << 4,       // +1_000_000 path penalty (ground), kill non-flying when stepped on
};

constexpr TileFlags operator|(TileFlags a, TileFlags b) {
  return static_cast<TileFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr TileFlags operator&(TileFlags a, TileFlags b) {
  return static_cast<TileFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
constexpr TileFlags operator~(TileFlags a) { return static_cast<TileFlags>(~static_cast<std::uint32_t>(a)); }

constexpr TileFlags& operator|=(TileFlags& a, TileFlags b) { return a = (a | b); }
constexpr TileFlags& operator&=(TileFlags& a, TileFlags b) { return a = (a & b); }

constexpr bool any(TileFlags v) { return static_cast<std::uint32_t>(v) != 0; }

constexpr bool has_flag(TileFlags v, TileFlags flag) { return any(v & flag); }

struct Tile {
  TileFlags flags = TileFlags::None;
};

class Map {
public:
  struct Snapshot {
    int width = 0;
    int height = 0;
    std::uint64_t version = 1;
    std::vector<TileFlags> flags{};
  };

  Map() = default;
  Map(int width, int height);

  int width() const { return width_; }
  int height() const { return height_; }
  std::uint64_t version() const { return version_; }

  bool in_bounds(TileCoord t) const;
  bool in_bounds(int x, int y) const { return in_bounds(TileCoord{x, y}); }

  bool in_world_bounds(const vec<f32>& p) const;
  vec<f32> clamp_to_bounds(const vec<f32>& p) const;

  const Tile& at(TileCoord t) const;
  Tile& at(TileCoord t);

  TileFlags flags(TileCoord t) const { return at(t).flags; }

  void set_flags(TileCoord t, TileFlags flags);
  void set_flag(TileCoord t, TileFlags flag, bool enabled);

  bool melee_deployable(TileCoord t) const { return has_flag(flags(t), TileFlags::MeleeDeployable); }
  bool ranged_deployable(TileCoord t) const { return has_flag(flags(t), TileFlags::RangedDeployable); }

  bool passable(TileCoord t, MoveMode mode) const;
  bool is_hole(TileCoord t) const { return has_flag(flags(t), TileFlags::Hole); }

  // Ground only: obstacle/hole have huge penalties. Air ignores penalties.
  int obstacle_penalty(TileCoord t, MoveMode mode) const;

  // Banker's rounding (round-half-to-even).
  static int round_half_to_even(double v);
  static TileCoord tile_at(const vec<f32>& pos);
  static vec<f32> tile_center(TileCoord t) { return vec<f32>{static_cast<f32>(t.x), static_cast<f32>(t.y)}; }

  Snapshot snapshot() const;
  void restore(const Snapshot& snap);

private:
  int width_ = 0;
  int height_ = 0;
  std::vector<Tile> tiles_;
  std::uint64_t version_ = 1;

  std::size_t index(TileCoord t) const { return static_cast<std::size_t>(t.y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(t.x); }
};

} // namespace arksim
