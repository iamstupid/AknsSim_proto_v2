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

struct ArknightsLevel;

// Shared per-simulation context that does not belong in ECS:
// - Map + navigation caches
// - spatial acceleration structures
// - reusable scratch buffers to avoid per-tick allocations
struct SimContext {
  struct Snapshot {
    Map::Snapshot map;
  };

  Map map{};
  BresenhamCache bresenham{};
  PathMapCache path_cache{map, bresenham};
  SpatialIndex spatial{};
  TargetSelectorScratch target_scratch{};
  Projectile::Scratch projectile_scratch{};

  // Reusable entity list for deterministic sorted iteration inside frame pipelines.
  std::vector<Entity> entities{};

  // Optional: Arknights stage data used by StageRuntime.
  const ArknightsLevel* arknights_level = nullptr;

  void reset_map(int width, int height) {
    map = Map(width, height);
    spatial.reset(width, height);
    path_cache.clear();
    arknights_level = nullptr;
  }

  Snapshot snapshot() const {
    Snapshot snap;
    snap.map = map.snapshot();
    return snap;
  }

  void restore(const Snapshot& snap) {
    map.restore(snap.map);
    spatial.reset(map.width(), map.height());
    path_cache.clear();
    entities.clear();
    target_scratch.candidates.clear();
    target_scratch.scored.clear();
    target_scratch.targets.clear();
    projectile_scratch.candidates.clear();
    projectile_scratch.hit_targets.clear();
    arknights_level = nullptr;
  }
};

} // namespace arksim
