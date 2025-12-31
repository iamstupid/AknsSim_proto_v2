#pragma once

#include <cstdint>

#include "ecs_lab/ecs.hpp"

namespace arksim {

using World = ecs_lab::World;
using Entity = ecs_lab::Entity;
using EntityProxy = ecs_lab::EntityProxyRef;

inline std::uint64_t entity_key(Entity e) {
  // Stable identifier for "name"/key usage (e.g. DamageAggregator, TriggerProcessor).
  // Do NOT use entity_idx for keys: indices are reused after destroy.
  return e.entity_id;
}

} // namespace arksim
