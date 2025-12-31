#pragma once

#include <vector>

#include "sim_core/nav/bresenham_cache.hpp"
#include "sim_core/ecs/ecs.hpp"
#include "sim_core/nav/map.hpp"
#include "sim_core/nav/path_map.hpp"
#include "sim_core/spatial/spatial_grid.hpp"
#include "sim_core/components/projectile.hpp"
#include "sim_core/spatial/target_selector.hpp"

namespace arksim {

// Shared per-simulation context that does not belong in ECS:
// - Map + navigation caches
// - spatial acceleration structures
// - reusable scratch buffers to avoid per-tick allocations
struct SimContext {
  Map map{};
  BresenhamCache bresenham{};
  PathMapCache path_cache{map, bresenham};
  SpatialIndex spatial{};
  TargetSelectorScratch target_scratch{};
  Projectile::Scratch projectile_scratch{};

  // Reusable entity list for deterministic sorted iteration inside frame pipelines.
  std::vector<Entity> entities{};

  void reset_map(int width, int height) {
    map = Map(width, height);
    spatial.reset(width, height);
    path_cache.clear();
  }
};

} // namespace arksim
