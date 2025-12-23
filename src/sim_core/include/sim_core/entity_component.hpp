#pragma once

#include <type_traits>

#include <flecs.h>

#include "sim_core/types.hpp"

namespace arksim {

struct EntityComponent {
  flecs::world* world = nullptr;
  flecs::entity entity{};
  TriggerProcessor<flecs::world&, flecs::entity, EntityComponent&> OnDestroy;
  bool destroyed = false;

  void bind(flecs::world& w, flecs::entity e) {
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
      component.entity.destruct();
    }
  }
}

} // namespace arksim
