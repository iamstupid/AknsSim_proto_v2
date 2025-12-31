#pragma once

#include <type_traits>

#include "sim_core/ecs/destroyed.hpp"
#include "sim_core/ecs/ecs.hpp"
#include "sim_core/core/types.hpp"

namespace arksim {

struct EntityComponent {
  World* world = nullptr;
  Entity entity{};
  TriggerProcessor<World&, Entity, EntityComponent&> OnDestroy;
  bool destroyed = false;

  void bind(World& w, Entity e) {
    world = &w;
    entity = e;
  }
};

template <typename T>
void Destroy(T& component) {
  static_assert(std::is_base_of<EntityComponent, T>::value, "Destroy expects EntityComponent-derived type");

  component.destroyed = true;
  if (component.world) {
    component.OnDestroy(*component.world, component.entity, component);
    if (component.destroyed) {
      mark_destroyed(*component.world, component.entity);
    }
  }
}

} // namespace arksim
