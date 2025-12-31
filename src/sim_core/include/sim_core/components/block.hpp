#pragma once

#include <cstdint>
#include <vector>

#include "sim_core/core/types.hpp"
#include "sim_core/core/vec.hpp"
#include "sim_core/ecs/ecs.hpp"

namespace arksim {

class SpatialIndex;

struct Blocker {
  static constexpr f32 kDefaultBlockRadius = 0.70710678f; // ~= sqrt(2)/2

  // 1 tick = 1 / 2^30 s
  static constexpr Tick kTicksPerSecond = Tick{1} << 30;

  // Default: scan every 3 frames (assuming 0.1s per frame) => 0.3s.
  static constexpr Tick kDefaultScanInterval = (kTicksPerSecond * 3 + 9) / 10; // ceil(0.3s)

  // Forced move to stable block position after engaging.
  static constexpr Tick kForcedMoveDuration = (kTicksPerSecond + 4) / 5; // ceil(0.2s)

  bool active = true;

  f32 block_radius = kDefaultBlockRadius;
  std::uint32_t block_capacity = 1;

  // Scan interval for finding new block targets (ticks).
  Tick scan_interval = kDefaultScanInterval;
  Tick scan_remain = 0;

  // Enemies currently blocked by this blocker.
  std::vector<Entity> blocked{};

  void step(World& world, Entity self, const SpatialIndex& spatial, Tick tick_rate);
};

} // namespace arksim

