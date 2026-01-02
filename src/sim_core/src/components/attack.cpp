#include "sim_core/components/attack.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "sim_core/components/destroyed.hpp"
#include "sim_core/components/position.hpp"
#include "sim_core/runtime/sim_state.hpp"

namespace arksim {
namespace {

enum class Facing4 : std::uint8_t {
  Right = 0,
  Up = 1,
  Left = 2,
  Down = 3,
};

Facing4 facing_from_dir(const vec<f32>& dir) {
  const f32 ax = std::abs(dir.x);
  const f32 ay = std::abs(dir.y);

  if (!(ax > 0.0f) && !(ay > 0.0f)) {
    return Facing4::Right;
  }

  if (ax >= ay) {
    return (dir.x >= 0.0f) ? Facing4::Right : Facing4::Left;
  }
  return (dir.y >= 0.0f) ? Facing4::Up : Facing4::Down;
}

TileCoord rotate_offset(TileCoord o, Facing4 facing) {
  switch (facing) {
    case Facing4::Right:
      return o;
    case Facing4::Up:
      // (x, y) -> (-y, x)
      return TileCoord{-o.y, o.x};
    case Facing4::Left:
      // (x, y) -> (-x, -y)
      return TileCoord{-o.x, -o.y};
    case Facing4::Down:
      // (x, y) -> (y, -x)
      return TileCoord{o.y, -o.x};
  }
  return o;
}

} // namespace

std::uint32_t Attack::atk_speed_mul_(float atk_speed) {
  float min_as = Attack::atk_speed_min;
  float max_as = Attack::atk_speed_max;
  if (min_as > max_as) {
    std::swap(min_as, max_as);
  }

  const float clamped = std::clamp(atk_speed, min_as, max_as);
  const double mul = 100.0 + static_cast<double>(clamped);
  const double mul_scaled = std::ceil(mul * static_cast<double>(kMulScale));
  if (!(mul_scaled > 0.0)) {
    return 1;
  }
  if (mul_scaled >= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(mul_scaled);
}

std::uint64_t Attack::phase_threshold_den_(Tick target_scaled_ticks) {
  if (target_scaled_ticks == 0) {
    return 0;
  }

  // We complete when:
  //   ceil(progress/kMulDen) >= target
  // <=> progress >= (target-1)*kMulDen + 1
  const std::uint64_t t = static_cast<std::uint64_t>(target_scaled_ticks - 1);
  if (t > (std::numeric_limits<std::uint64_t>::max() - 1ull) / kMulDen) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return t * kMulDen + 1ull;
}

Tick Attack::ceil_div_den_(std::uint64_t x) {
  if (x >= std::numeric_limits<std::uint64_t>::max() - (kMulDen - 1ull)) {
    return std::numeric_limits<Tick>::max();
  }
  return static_cast<Tick>((x + (kMulDen - 1ull)) / kMulDen);
}

void Attack::add_progress_(std::uint64_t& progress, Tick tick_rate, std::uint32_t mul) {
  if (progress == std::numeric_limits<std::uint64_t>::max()) {
    return;
  }

  const std::uint64_t a = static_cast<std::uint64_t>(tick_rate);
  const std::uint64_t b = static_cast<std::uint64_t>(mul);
  if (b == 0) {
    return;
  }

  const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
  if (a > (max - progress) / b) {
    progress = max;
    return;
  }
  progress += a * b;
}

bool Attack::timer_due_(Tick& remain, Tick interval, Tick tick_rate) {
  if (interval == 0) {
    return true;
  }
  if (remain == 0 || remain <= tick_rate) {
    remain = interval;
    return true;
  }
  remain -= tick_rate;
  return false;
}

void Attack::reset_to_idle_(bool force_scan) {
  state = State::Idle;
  pre_progress = 0;
  post_progress = 0;
  interval_progress = 0;
  pre_elapsed_scaled = 0;
  post_elapsed_scaled = 0;
  interval_target = 0;
  cached_candidates.clear();
  cached_world_tiles.clear();
  if (force_scan) {
    scan_remain = 0;
  }
}

void Attack::enter_pre_delay_() {
  state = State::PreDelay;
  pre_progress = 0;
  post_progress = 0;
  interval_progress = 0;
  pre_elapsed_scaled = 0;
  post_elapsed_scaled = 0;
  interval_target = 0;
}

void Attack::enter_post_delay_() {
  state = State::PostDelay;
  post_progress = 0;
  interval_progress = 0;
  post_elapsed_scaled = 0;
  interval_target = 0;
}

void Attack::enter_interval_await_() {
  state = State::IntervalAwait;

  const Tick used_pre = pre_elapsed_scaled;
  const Tick used_post = post_elapsed_scaled;
  if (base_interval <= used_pre + used_post) {
    interval_target = 0;
  } else {
    interval_target = base_interval - used_pre - used_post;
  }
  interval_progress = 0;
}

bool Attack::scan_(World& world,
                   Entity self,
                   const SpatialIndex& spatial,
                   TargetSelectorScratch& scratch,
                   bool update_cache) {
  std::vector<SpatialEntry>& candidates = update_cache ? cached_candidates : scratch.candidates;
  candidates.clear();

  const auto* pos = world.try_get<Position>(self);
  if (!pos) {
    if (update_cache) {
      cached_candidates.clear();
    }
    return false;
  }

  const vec<f32> center = pos->pos + center_offset;

  const SpatialGrid* grid = nullptr;
  switch (range_grid) {
    case TargetRange::Grid::Occupation:
      grid = &spatial.occupation;
      break;
    case TargetRange::Grid::Center:
      grid = &spatial.center;
      break;
  }
  if (!grid) {
    if (update_cache) {
      cached_candidates.clear();
    }
    return false;
  }

  switch (range_kind) {
    case TargetRange::Kind::Tiles: {
      std::span<const TileCoord> tiles;
      if (range_tiles_relative) {
        cached_world_tiles.clear();
        cached_world_tiles.reserve(range_tiles.size());

        const TileCoord base = Map::tile_at(center);
        const Facing4 facing = range_tiles_rotate_with_dir ? facing_from_dir(pos->dir) : Facing4::Right;

        for (const TileCoord off : range_tiles) {
          const TileCoord rot = range_tiles_rotate_with_dir ? rotate_offset(off, facing) : off;
          cached_world_tiles.push_back(TileCoord{base.x + rot.x, base.y + rot.y});
        }

        if (!cached_world_tiles.empty()) {
          tiles = std::span<const TileCoord>(cached_world_tiles.data(), cached_world_tiles.size());
        }
      } else if (!range_tiles.empty()) {
        tiles = std::span<const TileCoord>(range_tiles.data(), range_tiles.size());
      }
      grid->collect_tiles(tiles, required, candidates);
      break;
    }
    case TargetRange::Kind::Circle: {
      if (range_grid == TargetRange::Grid::Occupation && occupation_circle_intersect) {
        spatial.occupation.collect_circle_intersect(center, range_radius, required, candidates);
      } else {
        grid->collect_circle(center, range_radius, required, candidates);
      }
      break;
    }
  }

  // Valid target check: apply arranger filters (but avoid selecting a large set).
  TargetArranger check = arranger;
  check.source = self;
  check.max_targets = 1;
  check.arrange(world, candidates, scratch);
  return !scratch.targets.empty();
}

void Attack::fire_(World& world, SimState& sim, Entity self, TargetSelectorScratch& scratch) {
  scratch.targets.clear();
  scratch.scored.clear();
  if (cached_candidates.empty()) {
    return;
  }

  TargetArranger arr = arranger;
  arr.source = self;
  arr.arrange(world, cached_candidates, scratch);
  const auto targets = std::span<const Entity>(scratch.targets.data(), scratch.targets.size());
  if (targets.empty()) {
    return;
  }
  OnFire(world, &sim, self, targets, *this);
}

bool Attack::cancel_recovery_and_rescan(World& world,
                                       SimState& sim,
                                       Entity self,
                                       const SpatialIndex& spatial,
                                       Tick tick_rate,
                                       TargetSelectorScratch& scratch) {
  if (!active) {
    return false;
  }
  if (state != State::PostDelay && state != State::IntervalAwait) {
    return false;
  }

  reset_to_idle_(true);

  // Immediate rescan.
  if (scan_(world, self, spatial, scratch, true)) {
    enter_pre_delay_();

    // We just scanned; schedule the next scan after the interval.
    if (scan_interval != 0) {
      scan_remain = scan_interval;
    }
  }

  (void)sim;
  (void)tick_rate;
  return true;
}

void Attack::step(World& world,
                 SimState& sim,
                 Entity self,
                 const SpatialIndex& spatial,
                 Tick tick_rate,
                 TargetSelectorScratch& scratch) {
  if (!active) {
    return;
  }
  if (!world.is_alive(self) || world.has<Destroyed>(self)) {
    return;
  }

  const bool do_scan = (state == State::Idle || state == State::PreDelay) && timer_due_(scan_remain, scan_interval, tick_rate);

  if (state == State::Idle) {
    if (do_scan) {
      if (scan_(world, self, spatial, scratch, true)) {
        enter_pre_delay_();
      } else {
        cached_candidates.clear();
      }
    }
    return;
  }

  if (state == State::PreDelay) {
    if (do_scan) {
      if (!scan_(world, self, spatial, scratch, true)) {
        reset_to_idle_(false);
        return;
      }
    }

    const std::uint32_t mul = atk_speed_mul_(atk_speed);
    add_progress_(pre_progress, tick_rate, mul);

    if (base_pre == 0 || pre_progress >= phase_threshold_den_(base_pre)) {
      pre_elapsed_scaled = ceil_div_den_(pre_progress);
      fire_(world, sim, self, scratch);
      enter_post_delay_();
      return;
    }
    return;
  }

  if (state == State::PostDelay) {
    const std::uint32_t mul = atk_speed_mul_(atk_speed);
    add_progress_(post_progress, tick_rate, mul);

    if (base_post == 0 || post_progress >= phase_threshold_den_(base_post)) {
      post_elapsed_scaled = ceil_div_den_(post_progress);
      enter_interval_await_();
      // When recovery finishes quickly, scan as soon as we are idle again.
      if (interval_target == 0) {
        reset_to_idle_(true);
      }
      return;
    }
    return;
  }

  // IntervalAwait
  if (interval_target == 0) {
    reset_to_idle_(true);
    return;
  }

  const std::uint32_t mul = atk_speed_mul_(atk_speed);
  add_progress_(interval_progress, tick_rate, mul);
  if (interval_progress >= phase_threshold_den_(interval_target)) {
    reset_to_idle_(true);
    return;
  }
}

} // namespace arksim
