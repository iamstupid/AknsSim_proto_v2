#pragma once

#include <vector>

#include "sim_core/components/destroyed.hpp"
#include "sim_core/ecs/ecs.hpp"

namespace arksim {

inline void mark_destroyed(World& world, Entity e) {
  if (!world.is_alive(e)) {
    return;
  }
  if (world.has<Destroyed>(e)) {
    return;
  }
  world.add<Destroyed>(e);
}

inline void cleanup_destroyed(World& world, std::vector<Entity>& pending) {
  pending.clear();
  world.each<Destroyed>([&](Entity e, Destroyed&) { pending.push_back(e); });
  for (Entity e : pending) {
    world.destroy(e);
  }
}

inline void cleanup_destroyed(World& world) {
  std::vector<Entity> pending;
  cleanup_destroyed(world, pending);
}

} // namespace arksim
