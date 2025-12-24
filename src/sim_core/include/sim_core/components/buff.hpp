#pragma once

#include <cstdint>
#include <vector>

#include "sim_core/ecs.hpp"
#include "sim_core/components/damage.hpp"
#include "sim_core/types.hpp"

namespace arksim {

struct Buff {
  Entity giver{};
  std::vector<Entity> targets;
  std::int64_t life_remain = -1; // -1 means infinite
  TriggerProcessor<World&, Entity, Buff&> OnDestruct;

  void add_target(Entity target);
  void step(World& world, Entity self, Tick tick_rate);
  void destruct(World& world, Entity self);
};

Entity make_buff(World& world,
                 Entity target,
                 std::int64_t life_remain = -1,
                 Entity giver = {});
void link_buff_damage_processor(World& world,
                                Entity buff_entity,
                                Entity target,
                                DefStats& stats,
                                int priority,
                                DamageProcessor proc);

} // namespace arksim
