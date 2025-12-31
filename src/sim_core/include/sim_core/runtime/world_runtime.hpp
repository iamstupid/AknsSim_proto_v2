#pragma once

#include "sim_core/core/types.hpp"
#include "sim_core/ecs/ecs.hpp"

namespace arksim {

class SimState;

// World-level runtime/event hooks (stored in the ECS world via a singleton entity).
struct WorldRuntime {
  // Called when an entity "leaks" (reaches end).
  TriggerProcessor<World&, SimState*, Entity> OnLeak{};
};

} // namespace arksim

