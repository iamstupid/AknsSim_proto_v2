#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "sim_core/components/position.hpp"
#include "sim_core/nav/map.hpp"
#include "sim_core/core/types.hpp"
#include "sim_core/core/vec.hpp"

namespace arksim {

constexpr Tick kTicksPerSecond = Tick{1} << 30;

inline f32 tick_rate_to_seconds_f32(Tick tick_rate) {
  return static_cast<f32>(tick_rate) / static_cast<f32>(kTicksPerSecond);
}

inline vec<f32> normalize_or_zero(const vec<f32>& v) {
  if (!(v.length_sq() > 0.0f)) {
    return vec<f32>{};
  }
  return v.normalized();
}

inline vec<f32> clamp_magnitude(vec<f32> v, f32 max_len) {
  if (!(max_len > 0.0f)) {
    return vec<f32>{};
  }
  const f32 lsq = v.length_sq();
  if (!(lsq > max_len * max_len)) {
    return v;
  }
  const f32 inv = max_len / v.length();
  return v * inv;
}

inline f32 distance(const vec<f32>& a, const vec<f32>& b) {
  return (b - a).length();
}

inline vec<f32> remove_projection(const vec<f32>& v, const vec<f32>& dir_unit) {
  if (!(dir_unit.length_sq() > 0.0f)) {
    return v;
  }
  const vec<f32> du = dir_unit.normalized();
  const f32 k = dot(v, du);
  return v - du * k;
}

inline vec<f32> position_velocity(const Position& pos) {
  if (!(pos.current_speed > 0.0f) || !(pos.dir.length_sq() > 0.0f)) {
    return vec<f32>{};
  }
  return pos.dir.normalized() * pos.current_speed;
}

inline void set_position_velocity(Position& pos, const vec<f32>& vel) {
  const f32 speed = vel.length();
  pos.current_speed = speed;
  if (speed > 1e-6f) {
    pos.dir = vel / speed;
  }
}

inline vec<f32> find_nearest_passable_in_8(const Map& map, TileCoord center, MoveMode mode) {
  const TileCoord dirs[] = {
      TileCoord{-1, -1}, TileCoord{0, -1}, TileCoord{1, -1}, TileCoord{-1, 0},
      TileCoord{1, 0},   TileCoord{-1, 1}, TileCoord{0, 1},  TileCoord{1, 1},
  };

  const vec<f32> c = Map::tile_center(center);
  bool found = false;
  vec<f32> best = c;
  f32 best_d2 = 0.0f;
  for (TileCoord d : dirs) {
    const TileCoord t{center.x + d.x, center.y + d.y};
    if (!map.in_bounds(t)) {
      continue;
    }
    if (!map.passable(t, mode)) {
      continue;
    }
    const vec<f32> p = Map::tile_center(t);
    const vec<f32> diff = p - c;
    const f32 d2 = diff.length_sq();
    if (!found || d2 < best_d2) {
      found = true;
      best = p;
      best_d2 = d2;
    }
  }
  return best;
}

inline vec<f32> compute_avoidance_force(const Map& map,
                                        MoveMode mode,
                                        const vec<f32>& entity_pos,
                                        const vec<f32>& cursor_pos,
                                        const vec<f32>& foot_offset,
                                        f32 half_body_width,
                                        const vec<f32>& given_dir) {
  const TileCoord center_tile = Map::tile_at(cursor_pos);
  const vec<f32> center_center = Map::tile_center(center_tile);

  // Case A: center tile is not passable.
  if (!map.passable(center_tile, mode)) {
    const vec<f32> best = find_nearest_passable_in_8(map, center_tile, mode);
    const vec<f32> a = best - center_center;
    return remove_projection(normalize_or_zero(a), given_dir);
  }

  // Case B: scan 8 neighbors for blocked tiles (unpassable or obstacle/hole).
  const vec<f32> foot_pos = entity_pos + foot_offset;
  const vec<f32> left_p = foot_pos + vec<f32>{-half_body_width, 0.0f};
  const vec<f32> mid_p = foot_pos;
  const vec<f32> right_p = foot_pos + vec<f32>{+half_body_width, 0.0f};

  vec<f32> sum{};

  for (int oy = -1; oy <= 1; ++oy) {
    for (int ox = -1; ox <= 1; ++ox) {
      if (ox == 0 && oy == 0) {
        continue;
      }
      const TileCoord nb{center_tile.x + ox, center_tile.y + oy};
      if (!map.in_bounds(nb)) {
        continue;
      }

      const TileFlags f = map.flags(nb);
      const bool blocked = !map.passable(nb, mode) || has_flag(f, TileFlags::Obstacle) || has_flag(f, TileFlags::Hole);
      if (!blocked) {
        continue;
      }

      const vec<f32> rel = Map::tile_center(nb) - center_center; // {-1,0,1} offsets
      const vec<f32> nearest_point = (rel.x < 0.0f) ? left_p : (rel.x > 0.0f) ? right_p : mid_p;

      const vec<f32> v = nearest_point - center_center;

      const f32 oxp = std::max(v.x * rel.x, 0.0f);
      const f32 oyp = std::max(v.y * rel.y, 0.0f);

      const f32 ex = (oxp - 0.25f) * std::abs(rel.x);
      const f32 ey = (oyp - 0.25f) * std::abs(rel.y);

      vec<f32> contrib{};
      const bool is_side = (std::abs(rel.x) + std::abs(rel.y) == 1.0f);
      const bool is_diag = (std::abs(rel.x) == 1.0f && std::abs(rel.y) == 1.0f);

      if (is_side && (ex > 0.0f || ey > 0.0f)) {
        contrib = vec<f32>{-ex * rel.x, -ey * rel.y};
      } else if (is_diag && (ex > 0.0f && ey > 0.0f)) {
        const f32 avg = (ex + ey) * 0.5f;
        contrib = -avg * vec<f32>{rel.x, rel.y};
      }

      sum += contrib;
    }
  }

  const vec<f32> a2 = normalize_or_zero(sum);
  return remove_projection(a2, given_dir);
}

inline vec<f32> apply_displacement_with_correction(const Map& map,
                                                   MoveMode mode,
                                                   Position& pos,
                                                   const vec<f32>& disp,
                                                   f32 dt) {
  const vec<f32> start = pos.pos;
  vec<f32> dest = start + disp;
  vec<f32> vel = (dt > 0.0f) ? (disp / dt) : vec<f32>{};

  const TileCoord start_tile = Map::tile_at(start);
  const TileCoord dest_tile = Map::tile_at(dest);

  if (start_tile != dest_tile) {
    if (!map.passable(dest_tile, mode)) {
      const vec<f32> n = normalize_or_zero(Map::tile_center(dest_tile) - start);
      const f32 proj_k = dot(disp, n);
      const vec<f32> proj = n * proj_k;
      const vec<f32> corrected = disp - 2.0f * proj;
      dest = start + corrected;
      vel = (dt > 0.0f) ? (corrected / dt) : vec<f32>{};
    }
  }

  pos.pos = dest;
  return vel;
}

} // namespace arksim

